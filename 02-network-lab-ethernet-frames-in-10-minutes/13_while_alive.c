/* Ethernet Frames in 10 Minutes: MAC Addresses, Headers and Payloads - slide 13: thank you (C version of 13_while_alive.cpp) */
/* Build: make 13_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stdio.h>

/* the imaginary tomjnet.h of the slide; C has no std::string, so a topic owns a fixed char buffer */
typedef struct { char name[80]; } Topic;
static int episodes = 0;
static bool alive(void) { return episodes < 3; }             /* three episodes, then the demo ends */
static Topic next_network_topic(void) {
    static const char *topics[] = {"IP, TCP and UDP: what actually happens to a packet",
                                   "Sockets in C++: your first network program",
                                   "Inside the Linux network stack: from socket to NIC"};
    Topic t;
    snprintf(t.name, sizeof t.name, "%s", topics[episodes++]);
    return t;
}
static Topic viewer_request(void) {
    Topic t;
    snprintf(t.name, sizeof t.name, "viewer request #%d", episodes);
    return t;
}
static void subscribe(void) { printf("  subscribed to TomJNet\n"); }

/* C has no std::queue: a fixed array with a head and a tail index is the same first in first out */
enum { kWireSlots = 8 };
typedef struct { Topic slot[kWireSlots]; int head; int tail; } Queue;
static void push(Queue *q, Topic t) { if (q->tail < kWireSlots) q->slot[q->tail++] = t; }
static bool empty(const Queue *q) { return q->head == q->tail; }

int main(void) {
    static Queue wire;                  /* static: starts zeroed, head == tail == 0 */
    while (alive()) {
        push(&wire, next_network_topic());   /* IP, TCP and UDP */
        push(&wire, viewer_request());       /* payload of the frame */
        subscribe();                         /* lifetime benefit */
    }

    printf("frames on the wire, first in first out:\n");
    while (!empty(&wire)) {
        printf("  %s\n", wire.slot[wire.head].name);
        ++wire.head;
    }
    return 0;
}
