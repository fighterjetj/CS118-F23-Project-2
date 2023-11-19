#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#include "utils.h"

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

unsigned int recv_ack(int sockfd, struct sockaddr_in *addr, socklen_t addr_size, struct timeval *timeout)
{
    unsigned int acknum;
    set_socket_timeout(sockfd, *timeout);
    int bytes_received = recvfrom(sockfd, &acknum, sizeof(acknum), 0, (struct sockaddr *)addr, &addr_size);
    if (bytes_received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            printf("Timeout\n");
            return -1;
        }
        perror("Error receiving ACK");
        exit(1);
    }
    printf("ACK %d\n", acknum);
    return acknum;
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

void update_timeout(struct timeval *curr_timeout, struct timeval *time_dev, struct timeval *time_elapsed)
{
    // Update this code as we implement a more complicated algorithm
    curr_timeout->tv_sec = TIMEOUT;
    curr_timeout->tv_usec = 0;
}

struct timeval get_time_elapsed(struct timeval *start_time)
{
    struct timeval curr_time;
    gettimeofday(&curr_time, NULL);
    struct timeval time_elapsed;
    timersub(&curr_time, start_time, &time_elapsed);
    return time_elapsed;
}

void increase_window_size(double *window_size, int ssthresh)
{
    // Update this code as we implement a more complicated algorithm
    *window_size = 1.0;
}

struct sent_packet
{
    struct packet pkt;
    struct timeval time_sent;
    int ack_received;
    int timed_out;
};

int main(int argc, char *argv[])
{
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);
    struct timeval est_rtt;
    struct timeval measured_rtt;
    struct timeval dev_rtt;
    struct packet pkt;
    char buffer[PAYLOAD_SIZE];
    unsigned int curr_seq_num = 0;
    unsigned int ack_num = 0;
    int bytes_read = 0;
    double window_size = 1.0;
    struct sent_packet sent_packets[MAX_WINDOW_SIZE];

    for (int i = 0; i < MAX_WINDOW_SIZE; i++)
    {
        sent_packets[i].ack_received = 0;
        sent_packets[i].timed_out = 0;
    }
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
    // TODO: Read from file, and initiate reliable data transfer to the server

    // Get file size
    fseek(fp, 0, SEEK_END);
    unsigned int file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    bytes_read = read_file_and_create_packet(fp, &pkt, 0);
    double number_of_packets = (double)file_size / (double)PAYLOAD_SIZE;
    int number_of_packets_int = (int)ceil(number_of_packets);
    unsigned int number_of_packets_to_send = (unsigned int)number_of_packets_int;
    printf("Number of packets to send: %d\n", number_of_packets_to_send);
    curr_seq_num++;
    // Send handshake
    send_handshake(number_of_packets_to_send, &pkt, send_sockfd, &server_addr_to, addr_size);
    // printPacket(&pkt);
    while (recv_ack(listen_sockfd, &server_addr_from, addr_size) != curr_seq_num)
    {
        send_handshake(file_size, &pkt, send_sockfd, &server_addr_to, addr_size);
    }
    /*
    Handshake format:
    1. 4 bytes for file size in packets
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
