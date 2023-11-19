#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

int write_packet_to_file(FILE *fp, struct packet *pkt)
{
    size_t bytes_written = fwrite(pkt->payload, 1, pkt->length, fp);
    if (bytes_written < 0)
    {
        perror("Error writing to file");
        exit(1);
    }
    return bytes_written;
}

void recv_packet(struct packet *pkt, int sockfd, struct sockaddr_in *addr, socklen_t addr_size)
{
    int bytes_received = recvfrom(sockfd, pkt, PACKET_SIZE, 0, (struct sockaddr *)addr, &addr_size);
    if (bytes_received < 0)
    {
        perror("Error receiving packet");
        exit(1);
    }
    printRecv(pkt);
}

void handle_recv_packet(unsigned int expected_seq_num, struct recvd_packet *packet_buffer, struct packet *pkt, int sockfd, struct sockaddr_in *addr, socklen_t addr_size)
{
    recv_packet(pkt, sockfd, addr, addr_size);
}

unsigned int handle_handshake(FILE *fp, struct packet *pkt, int sockfd, struct sockaddr_in *addr, socklen_t addr_size)
{
    recv_packet(pkt, sockfd, addr, addr_size);
    unsigned int file_length = pkt->seqnum;
    return file_length;
}

// Our ACK messages are just the number sent and nothing else
void send_ack(unsigned int acknum, int sockfd, struct sockaddr_in *addr, socklen_t addr_size)
{
    int bytes_sent = sendto(sockfd, &acknum, sizeof(acknum), 0, (struct sockaddr *)addr, addr_size);
    if (bytes_sent < 0)
    {
        perror("Error sending ACK");
        exit(1);
    }
    printf("ACK %d\n", acknum);
}

// Storing a received packet and whether it has been received
struct recvd_packet
{
    int received;
    struct packet pkt;
};

int main()
{
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    struct packet buffer;
    socklen_t addr_size = sizeof(client_addr_from);
    int expected_seq_num = 0;
    int largest_received_seq_num = 0;
    int recv_len;
    unsigned int ack;
    struct recvd_packet recv_packets_buffer[MAX_WINDOW_SIZE];
    // We haven't received any packets yet
    for (int i = 0; i < MAX_WINDOW_SIZE; i++)
    {
        recv_packets_buffer[i].received = 0;
    }

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0)
    {
        perror("Could not create send socket");
        return 1;
    }

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0)
    {
        perror("Could not create listen socket");
        return 1;
    }

    // Configure the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the server address
    if (bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Configure the client address structure to which we will send ACKs
    memset(&client_addr_to, 0, sizeof(client_addr_to));
    client_addr_to.sin_family = AF_INET;
    client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST);
    client_addr_to.sin_port = htons(CLIENT_PORT_TO);

    // Open the target file for writing (always write to output.txt)
    FILE *fp = fopen("output.txt", "wb");

    // TODO: Receive file from the client and save it as output.txt
    /*
    Handshake: File size
    */
    unsigned int num_packets = handle_handshake(fp, &buffer, listen_sockfd, &client_addr_from, addr_size);
    expected_seq_num++;
    send_ack(expected_seq_num, send_sockfd, &client_addr_to, addr_size);
    /* Upon receiving a packet:
    Read the header
    If the sequence number is the next expected sequence number, ACK it
    If the sequence number is out of order buffer it and ACK the last in sequence packet
    If the sequence number is the last packet, ACK it and close the file
     */
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
