#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <math.h>

#include "utils.h"

struct sent_packet
{
    struct packet pkt;
    int resent;
    struct timeval time_sent;
};

void serve_packet(struct packet *pkt, int sockfd, struct sockaddr_in *addr, socklen_t addr_size)
{
    int bytes_sent = sendto(sockfd, pkt, pkt->length + HEADER_SIZE, 0, (struct sockaddr *)addr, addr_size);
    if (bytes_sent < 0)
    {
        perror("Error sending packet");
        exit(1);
    }
    printSend(pkt, 0);
}

void send_handshake(unsigned int file_size, struct packet *pkt, int sockfd, struct sockaddr_in *addr, socklen_t addr_size)
{
    // Setting the sequence number to the file size
    pkt->seqnum = file_size;
    printf("Sending handshake: ");
    serve_packet(pkt, sockfd, addr, addr_size);
}

void set_socket_timeout(int sockfd, struct timeval timeout)
{
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        perror("Error setting socket timeout");
        exit(1);
    }
}

int recv_ack(int sockfd, struct sockaddr_in *addr, socklen_t addr_size)
{
    unsigned int acknum;
    int bytes_received = recvfrom(sockfd, &acknum, sizeof(acknum), 0, (struct sockaddr *)addr, &addr_size);
    if (bytes_received < 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            // Timeout reached, return -2 to deal with it in the main
            printf("Timeout reached. No message received.\n");
            return -2;
        }
        else
        {
            perror("Recvfrom failed");
            return -1;
        }
    }
    else
    {
        // Process the received message
        printf("Received message: ");
    }
    printf("ACK %d\n", acknum);
    return (int)acknum;
}

// Function that reads in from the file and creates a packet with the next contents
int read_file_and_create_packet(FILE *fp, struct packet *pkt, unsigned int seq_num)
{
    // Read in the file
    char payload[PAYLOAD_SIZE];
    int bytes_read = fread(payload, 1, PAYLOAD_SIZE, fp);
    if (bytes_read < 0)
    {
        perror("Error reading file");
        exit(1);
    }
    // Build the packet
    build_packet(pkt, seq_num, bytes_read, payload);
    return bytes_read;
}

// Function that will buffer sent packets for us
int buffer_packet(struct packet *pkt, struct sent_packet *buffer, unsigned int *ack_num)
{
    int ind = pkt->seqnum - *ack_num;
    if (ind < 0)
    {
        printf("Already received ACK up to packet %d, which occurs after packet %d\n", *ack_num, pkt->seqnum);
        return -1;
    }
    if (ind >= MAX_BUFFER)
    {
        printf("Exceeded maximum window size with packet %d while waiting for ack for %d\n", pkt->seqnum, *ack_num);
    }
    buffer[ind].pkt = *pkt;
    buffer[ind].resent = 0;
    gettimeofday(&buffer[ind].time_sent, NULL);
    printf("Buffered packet %d\n", pkt->seqnum);
    return ind;
}

// Function that handles moving the buffer forward upon receiving an ACK
void handle_ack(struct sent_packet *buffer, unsigned int *old_ack, unsigned int *new_ack)
{
    int num_pkt_recv = *new_ack - *old_ack;
    if (num_pkt_recv <= 0)
    {
        printf("Received old ACK for %d\n", *new_ack);
        return;
    }
    if (num_pkt_recv > MAX_BUFFER)
    {
        perror("Received ACK that should not have been sent yet");
        return;
    }
    for (int i = num_pkt_recv; i < MAX_BUFFER; i++)
    {
        buffer[i - num_pkt_recv] = buffer[i];
    }
    *old_ack = *new_ack;
}

// Function that marks a packet as resent after resending it
void resend_packet(
    struct sent_packet *buffer,
    unsigned int *packet_num,
    unsigned int *ack_num,
    int sockfd,
    struct sockaddr_in *addr,
    socklen_t addr_size
    )
{
    int ind = packet_num - ack_num;
    if (ind < 0 || ind >= MAX_BUFFER)
    {
        printf("Can't resend packet %d - not currently buffered", *packet_num);
        return;
    }
    serve_packet(&buffer[ind].pkt, sockfd, addr, addr_size);
    buffer[ind].resent = 1;
}

void update_est_rtt(struct timeval *est_rtt, struct timeval *dev_rtt, struct timeval *sample_rtt)
{
    // Update the estimated RTT
    est_rtt->tv_sec = ((1.0 - ALPHA) * est_rtt->tv_sec) + (ALPHA * sample_rtt->tv_sec);
    est_rtt->tv_usec = ((1.0 - ALPHA) * est_rtt->tv_usec) + (ALPHA * sample_rtt->tv_usec);
    // Update the deviation RTT
    dev_rtt->tv_sec = ((1.0 - BETA) * dev_rtt->tv_sec) + (BETA * labs(sample_rtt->tv_sec - est_rtt->tv_sec));
    dev_rtt->tv_usec = ((1.0 - BETA) * dev_rtt->tv_usec) + (BETA * labs(sample_rtt->tv_usec - est_rtt->tv_usec));
}

void time_elapsed_since(struct timeval *start, struct timeval *end, struct timeval *elapsed)
{
    elapsed->tv_sec = end->tv_sec - start->tv_sec;
    elapsed->tv_usec = end->tv_usec - start->tv_usec;
    if (elapsed->tv_usec < 0)
    {
        elapsed->tv_sec--;
        elapsed->tv_usec += 1000000;
    }
}

void send_and_buffer_packet(
    struct packet *pkt,
    struct sent_packet *buffer,
    unsigned int *ack_num,
    int sockfd,
    struct sockaddr_in *addr,
    socklen_t addr_size
    )
{
    // Send the packet
    serve_packet(pkt, sockfd, addr, addr_size);
    // Buffer the packet
    int ind = buffer_packet(pkt, buffer, ack_num);
}

int main(int argc, char *argv[])
{
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);
    struct timeval timeout;
    struct timeval dev_rtt;
    struct packet pkt;
    unsigned int seq_num = 1;
    int ack_num = 0;
    int new_ack;
    int bytes_read;
    struct sent_packet buffer[MAX_BUFFER];
    for (int i = 0; i < MAX_BUFFER; i++)
    {
        buffer[i].resent = 0;
    }
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;
    dev_rtt.tv_sec = 0;
    dev_rtt.tv_usec = 0;

    // read filename from command line argument
    if (argc != 2)
    {
        printf("Usage: ./client <filename>\n");
        return 1;
    }
    char *filename = argv[1];

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0)
    {
        perror("Could not create listen socket");
        return 1;
    }

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0)
    {
        perror("Could not create send socket");
        return 1;
    }

    // Configure the server address structure to which we will send data
    memset(&server_addr_to, 0, sizeof(server_addr_to));
    server_addr_to.sin_family = AF_INET;
    server_addr_to.sin_port = htons(SERVER_PORT_TO);
    server_addr_to.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Configure the client address structure
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_PORT);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the client address
    if (bind(listen_sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
    {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Open file for reading
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    unsigned int file_size = ftell(fp);
    unsigned int num_packets = (unsigned int)ceil((double)file_size / PAYLOAD_SIZE);
    fseek(fp, 0, SEEK_SET);
    printf("Starting to send file: %s, which has size %d which will take %d packets\n", filename, file_size, num_packets);
    bytes_read = read_file_and_create_packet(fp, &pkt, 0);

    // Send handshake
    send_handshake(num_packets, &pkt, send_sockfd, &server_addr_to, addr_size);
    // printPacket(&pkt);
    set_socket_timeout(listen_sockfd, timeout);

    ack_num = recv_ack(listen_sockfd, &server_addr_from, addr_size);

    while (ack_num != 1)
    {
        // Send handshake
        send_handshake(num_packets, &pkt, send_sockfd, &server_addr_to, addr_size);
        // printPacket(&pkt);
        ack_num = recv_ack(listen_sockfd, &server_addr_from, addr_size);
    }
    printf("Handshake Received\n");
    // Changed the following <= to < for correct client shutdown if the server's final ACK is not lost
    while (ack_num < num_packets)
    {
        // The number of packets in flight is seq_num - ack_num
        int num_packets_in_flight = seq_num - ack_num;

        // In this case, we want to have a fixed cwnd size of 4. We want 4 packets to be flying
        // as often as possible. This only changes when we are reaching the end of the file and
        // we have less than 4 packets left to send. Therefore, the number of packets we want to
        // send is min(4 - num_packets_in_flight, num_packets - seq_num)
        int num_packets_to_send = fmin(4 - num_packets_in_flight, num_packets - seq_num);
        
        // Send the packets
        for (int i = 0; i < num_packets_to_send; i++){
            // Read in the next packet
            bytes_read = read_file_and_create_packet(fp, &pkt, seq_num);
            seq_num++;
            // Send packet
            send_and_buffer_packet(&pkt, buffer, &ack_num, send_sockfd, &server_addr_to, addr_size);
        }


        // Receive ack
        new_ack = recv_ack(listen_sockfd, &server_addr_from, addr_size);

        if (new_ack == -1)
        {
            // Treat the case in which recvfrom has failed
        }
        else if (new_ack == -2) 
        {
            // Treat the case in which recvfrom has timed out
            // Right now there are 4 flying packets, so the timeout is for the first packet in the
            // buffer. Resend this packet
            printf("There has been a timeout, resending first packet\n");
            serve_packet(&buffer[0].pkt, send_sockfd, &server_addr_to, addr_size);
        }
        else
        {
            // Treat the case in which an ack has been received
            handle_ack(buffer, &ack_num, &new_ack);
            ack_num = new_ack;
        }
        

        // while (new_ack != seq_num)
        // {
        //     // Resend packet
        //     resend_packet(buffer, &ack_num, &ack_num, send_sockfd, &server_addr_to, addr_size);
        //     // Receive ack
        //     new_ack = recv_ack(listen_sockfd, &server_addr_from, addr_size);
        // }

    }
    printf("File sent\n");

    /*
Handshake format:
1. 4 bytes for file size
2. 2 bytes for packet length
That leaves 1194 bytes for the payload
This is just the normal packet format, but instead of the sequence number, we have the file size
*/
    /* We need to read in the file
    As we read it in, we need to create a header formatted as follows:
    1. 4 bytes for the sequence number
    2. 2 bytes for packet length
    That leaves 1194 bytes for the payload
    Then we need to send the packet to the server
    Set a timeout timer
    If the ack is received, we terminate
    If we timeout, we resend
    */

    /*
    Roadmap:
    Send one packet to the server
    Ack that packet
    Send entire file and ack (one packet at a time)
    Send multiple packets at a time and ack (using fixed timeout and fixed window size)
    Send multiple packets at a time and ack (using variable window size)
    Tuning the system to get best efficiency
    */

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
