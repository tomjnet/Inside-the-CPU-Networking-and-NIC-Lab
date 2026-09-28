/* Polling vs Interrupts: Why Low Latency Systems Poll - slide 13: thank you (C version of 13_while_alive.cpp) */
/* Build: make 13_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* The imaginary tomjnet.h of the slide. C has no templates: the ring holds Topic and has a fixed capacity. */
typedef struct { char name[64]; } Topic;
#define RING_CAPACITY 8
typedef struct {
    Topic items[RING_CAPACITY];
    int count;
} Ring;
static void ring_push(Ring *r, Topic t) {
    if (r->count < RING_CAPACITY) r->items[r->count++] = t;
}
static int episodes = 0;
static bool alive(void) { return episodes < 3; }              /* three episodes, then the demo ends */
static Topic next_network_topic(void) {
    static const char *topics[] = {"Kernel Bypass: DPDK, AF_XDP and Low Latency Networking",
                                   "What We Learned About Networking and NICs",
                                   "Low Latency C++ Lab"};
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

/* #include "tomjnet.h"   (imaginary: the block above stands in for it) */

int main(void) {
    Ring rx;                              /* no interrupt needed */
    memset(&rx, 0, sizeof rx);
    while (alive()) {
        ring_push(&rx, next_network_topic());   /* next: kernel bypass */
        ring_push(&rx, viewer_request());       /* polled every episode */
        subscribe();                            /* lifetime benefit */
    }

    printf("polled from the ring:\n");
    for (int i = 0; i < rx.count; ++i) printf("  %s\n", rx.items[i].name);
    return 0;
}
