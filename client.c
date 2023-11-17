#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

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

int main(int argc, char *argv[])
{
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);
    struct timeval tv;
    struct packet pkt;
    struct packet ack_pkt;
    char buffer[PAYLOAD_SIZE];
    unsigned short seq_num = 0;
    unsigned short ack_num = 0;
    char last = 0;
    char ack = 0;

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

    struct packet handshake_pkt;
    char *handshake_payload = "Hello World!\0";
    build_packet(&handshake_pkt, file_size, 13, handshake_payload);
    printPacket(&handshake_pkt);
    serve_packet(&handshake_pkt, send_sockfd, &server_addr_to, addr_size);
    /*
    Handshake format:
    1. 4 bytes for file size
    2. 2 bytes for packet length
    That leaves 1194 bytes for the payload
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
