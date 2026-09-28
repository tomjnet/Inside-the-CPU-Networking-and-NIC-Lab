/* Computer Networking in 5 Minutes: How Data Moves Between Machines - slide 7: thank you (C version of 07_while_alive.cpp) */
/* Build: make 07_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* #include "tomjnet.h"  (imaginary on the slide: these are its contents) */
typedef struct { const char *name; } Topic;

/* C has no std::queue: a small ring buffer with a head index and a count does the same job. */
enum { QUEUE_CAPACITY = 4 };
typedef struct { Topic items[QUEUE_CAPACITY]; size_t head, count; } TopicQueue;
static bool queue_push(TopicQueue *q, Topic t) {
    if (q->count == QUEUE_CAPACITY) return false;
    q->items[(q->head + q->count) % QUEUE_CAPACITY] = t;
    ++q->count;
    return true;
}
static Topic queue_front(const TopicQueue *q) { return q->items[q->head]; }
static void queue_pop(TopicQueue *q) { q->head = (q->head + 1) % QUEUE_CAPACITY; --q->count; }
static bool queue_empty(const TopicQueue *q) { return q->count == 0; }

static int episodes = 0;
static bool alive(void) { return episodes < 3; }               /* three episodes, then the demo ends */
static Topic next_network_topic(void) {
    static const char *topics[] = {"Ethernet frames: MAC addresses, headers and payloads",
                                   "IP, TCP and UDP: what actually happens to a packet",
                                   "Sockets in C++: your first network program"};
    Topic t = {topics[episodes++]};
    return t;
}
static void send_topic(const Topic *t) { printf("next: %s (100 B + 46 B headers)\n", t->name); }
static void subscribe(void) { printf("  subscribed to TomJNet\n"); }

int main(void) {
    TopicQueue network_lab = {{{NULL}}, 0, 0};
    while (alive()) {
        if (!queue_push(&network_lab, next_network_topic())) return 1;  /* Ethernet frames */
        Topic front = queue_front(&network_lab);
        send_topic(&front);                      /* 100 B + 46 B headers */
        queue_pop(&network_lab);                 /* first in, first out */
        subscribe();                             /* lifetime benefit */
    }
    printf("queue empty: %s\n", queue_empty(&network_lab) ? "true" : "false");
    return 0;
}
