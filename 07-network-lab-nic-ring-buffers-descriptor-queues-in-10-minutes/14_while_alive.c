/* NIC Ring Buffers and Descriptor Queues Explained - slide 14: thank you (C version of 14_while_alive.cpp) */
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

/* the imaginary tomjnet.h of the slide; C has no std::string, so a Topic owns a fixed char buffer */
typedef struct { char name[64]; } Topic;
static size_t episodes = 0;
static const char *const plan[] = {
    "Polling vs Interrupts: Why Low Latency Systems Poll",
    "Kernel Bypass: DPDK, AF_XDP and Low Latency Networking",
    "Inside the CPU: What We Learned About Networking and NICs"};
static bool alive(void) { return episodes < 3; }                 /* three episodes left in this lab, then the demo ends */
static Topic next_network_topic(void) {
    Topic t;
    snprintf(t.name, sizeof t.name, "%s", plan[episodes++]);
    return t;
}
static Topic viewer_request(void) {
    Topic t;
    snprintf(t.name, sizeof t.name, "viewer request #%zu", episodes);
    return t;
}
static void subscribe(void) { printf("  subscribed to TomJNet\n"); }

/* C has no std::optional: pop returns a bool and writes the value through a pointer; NULL means empty */
static size_t watched = 0;
static void watch(const Topic *t) {
    if (t != NULL) { ++watched; printf("  watched: %s\n", t->name); }
}

/* the ring of the video in its smallest form: power of two capacity, push drops when full.
   C has no templates, so this ring holds Topic and its capacity is a named constant. */
enum { RING_N = 8 };
_Static_assert(RING_N > 0 && (RING_N & (RING_N - 1)) == 0, "capacity must be a power of two");
typedef struct {
    Topic buf[RING_N];
    size_t head;                         /* free running, masked on access */
    size_t tail;
    size_t missed;
} TopicRing;

static bool ring_push(TopicRing *r, Topic value) {
    if (r->tail - r->head == RING_N) { ++r->missed; return false; }
    r->buf[r->tail++ & (RING_N - 1)] = value;
    return true;
}
static bool ring_pop(TopicRing *r, Topic *out) {
    if (r->head == r->tail) return false;
    *out = r->buf[r->head++ & (RING_N - 1)];
    return true;
}
static size_t ring_size(const TopicRing *r) { return r->tail - r->head; }

int main(void) {
    static TopicRing lab;                /* power of two, no heap; zero initialised: empty */
    Topic t;
    while (alive()) {
        ring_push(&lab, next_network_topic());   /* polling vs interrupts */
        ring_push(&lab, viewer_request());       /* dropped only when full */
        watch(ring_pop(&lab, &t) ? &t : NULL);   /* clean and refill */
        subscribe();                             /* lifetime benefit */
    }

    printf("pushed %zu, watched %zu, still in the ring %zu, missed %zu\n",
           2 * episodes, watched, ring_size(&lab), lab.missed);
    while (ring_size(&lab) > 0) watch(ring_pop(&lab, &t) ? &t : NULL);   /* drain the ring before exit */
    return lab.missed == 0 && watched == 2 * episodes ? 0 : 1;
}
