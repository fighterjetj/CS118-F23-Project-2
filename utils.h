#ifndef UTILS_H
#define UTILS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MACROS
#define SERVER_IP "127.0.0.1"
#define LOCAL_HOST "127.0.0.1"
#define SERVER_PORT_TO 5002
#define CLIENT_PORT 6001
#define SERVER_PORT 6002
#define CLIENT_PORT_TO 5001
#define PAYLOAD_SIZE 1194
#define WINDOW_SIZE 5
#define TIMEOUT 2
#define MAX_SEQUENCE 1024
#define HEADER_SIZE 6

// Packet Layout
// You may change this if you want to
struct packet
{
    unsigned short length;
    unsigned int seqnum;
    char payload[PAYLOAD_SIZE];
};

// Utility function to build a packet
void build_packet(struct packet *pkt, unsigned int seqnum, unsigned short length, const char *payload)
{
    pkt->seqnum = seqnum;
    pkt->length = length;
    memcpy(pkt->payload, payload, length);
}

// Utility function to print a packet
void printRecv(struct packet *pkt)
{
    printf("RECV %d LENGTH %d\n", pkt->seqnum, pkt->length);
}

void printSend(struct packet *pkt, int resend)
{
    if (resend)
        printf("RESEND %d LENGTH %d\n", pkt->seqnum, pkt->length);
    else
        printf("SEND %d LENGTH %d\n", pkt->seqnum, pkt->length);
}

void printPacket(struct packet *pkt)
{
    printf("%s\n", pkt->payload);
}

#endif