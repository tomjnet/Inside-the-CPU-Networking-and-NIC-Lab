/* Inside the Linux Network Stack: From Socket to NIC - slide 13: thank you (C version of 13_while_alive.cpp) */
/* Build: make 13_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdio.h>

/* the imaginary tomjnet.h of the slide */
typedef struct { char name[64]; } Topic;            /* C has no std::string: a fixed char buffer */
static int episodes = 0;
static int alive(void) { return episodes < 3; }             /* three episodes, then the demo ends */
static Topic next_network_topic(void) {
    static const char *topics[] = {"How a NIC Works: RX, TX, DMA and Interrupts",
                                   "NIC Ring Buffers and Descriptor Queues Explained",
                                   "Polling vs Interrupts: Why Low Latency Systems Poll"};
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

/* C has no std::queue: a fixed array with a head and a tail index */
typedef struct { Topic items[8]; size_t head; size_t tail; } TopicQueue;
static void queue_push(TopicQueue *q, Topic t) { q->items[q->tail++ % 8] = t; }

int main(void) {
    TopicQueue todo = {0};
    while (alive()) {
        queue_push(&todo, next_network_topic()); /* next: how a NIC works */
        queue_push(&todo, viewer_request());     /* RX, TX, DMA, interrupts */
        subscribe();                             /* zero copies, one click */
    }

    printf("queued topics: %zu\n", todo.tail - todo.head);
    while (todo.head != todo.tail) {
        printf("  %s\n", todo.items[todo.head % 8].name);
        todo.head++;
    }
    return 0;
}
