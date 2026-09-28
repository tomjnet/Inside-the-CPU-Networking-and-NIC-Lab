/* Kernel Bypass: DPDK, AF_XDP and Low Latency Networking - slide 14: thank you (C version of 14_while_alive.cpp) */
/* Build: make 14_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* ---- the imaginary "tomjnet.h" of the slide ---- */
/* C has no std::string, so a Topic owns a fixed char buffer */
typedef struct { char name[64]; } Topic;
static int episodes = 0;
static bool alive(void) { return episodes < 3; }             /* three episodes, then the demo ends */
static Topic next_network_topic(void) {
    static const char *const topics[] = {"Inside the CPU: What We Learned About Networking and NICs",
                                         "the next lab of the channel",
                                         "a low latency feed handler, end to end"};
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

/* C has no templates and no std::vector: this ring holds Topic in a fixed array of 8 slots */
typedef struct {                                             /* a ring of topics: push at head, no system call */
    Topic slot[8];
    size_t head;
} RxRing;
static void ring_push(RxRing *r, Topic t) {
    printf("  ring slot %zu: %s\n", r->head & 7, t.name);
    r->slot[r->head++ & 7] = t;                              /* a struct copy: C has no std::move */
}
/* ---- end of tomjnet.h ---- */

int main(void) {
    RxRing ring = {0};
    while (alive()) {
        ring_push(&ring, next_network_topic());   /* the series summary */
        ring_push(&ring, viewer_request());       /* no system call needed */
        subscribe();                              /* zero copy benefit */
    }

    printf("%zu topics in the ring, next episode: the series summary\n", ring.head);
    return 0;
}
