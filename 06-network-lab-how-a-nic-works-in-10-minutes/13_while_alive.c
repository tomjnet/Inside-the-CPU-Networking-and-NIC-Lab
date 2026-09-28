/* How a NIC Works: RX, TX, DMA and Interrupts - slide 13: thank you (C version of 13_while_alive.cpp) */
/* Build: make 13_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdio.h>

/* The imaginary tomjnet.h of the slide, written out so the program builds and ends. */
typedef struct { char name[64]; } Topic;                    /* C has no std::string: a fixed char buffer */
static int episodes = 0;
static int alive(void) { return episodes < 3; }             /* three episodes, then the demo ends */
static Topic next_network_topic(void) {
    static const char *topics[] = {"NIC ring buffers and descriptor queues",
                                   "Polling vs interrupts: why low latency systems poll",
                                   "Kernel bypass: DPDK, AF_XDP and low latency networking"};
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

/* C has no std::queue: a small ring with a head and a tail index, like the NIC's own rings */
#define kRing 8u
typedef struct { Topic items[kRing]; size_t head; size_t tail; } TopicRing;
static void ring_push(TopicRing *r, Topic t) { r->items[r->tail++ % kRing] = t; }

int main(void) {
    TopicRing rx_ring = {0};                    /* next: descriptor rings */
    while (alive()) {
        ring_push(&rx_ring, next_network_topic()); /* NIC ring buffers */
        ring_push(&rx_ring, viewer_request());     /* no drops here */
        subscribe();                               /* lifetime benefit */
    }

    printf("queued topics, first in first out:\n");
    while (rx_ring.head != rx_ring.tail) {
        printf("  %s\n", rx_ring.items[rx_ring.head % kRing].name);
        rx_ring.head++;
    }
    return 0;
}
