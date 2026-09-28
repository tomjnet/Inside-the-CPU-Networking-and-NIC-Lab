/* IP, TCP and UDP: What Actually Happens to a Packet - slide 14: thank you (C version of 14_while_alive.cpp) */
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

/* C has no std::queue: a small ring buffer with a head index and a count does the same job. */
enum { QUEUE_CAPACITY = 8 };
typedef struct { Topic items[QUEUE_CAPACITY]; size_t head, count; } TopicQueue;
static bool queue_push(TopicQueue *q, Topic t) {
    if (q->count == QUEUE_CAPACITY) return false;
    q->items[(q->head + q->count) % QUEUE_CAPACITY] = t;
    ++q->count;
    return true;
}
static const Topic *queue_front(const TopicQueue *q) { return &q->items[q->head]; }
static void queue_pop(TopicQueue *q) { q->head = (q->head + 1) % QUEUE_CAPACITY; --q->count; }
static bool queue_empty(const TopicQueue *q) { return q->count == 0; }

static int episodes = 0;
static bool alive(void) { return episodes < 3; }                /* three episodes, then the demo ends */
static Topic next_network_topic(void) {
    static const char *topics[] = {"Sockets in C++: Your First Network Program",
                                   "Inside the Linux Network Stack: From Socket to NIC",
                                   "How a NIC Works: RX, TX, DMA and Interrupts"};
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

int main(void) {
    static TopicQueue todo;                     /* zero initialised: empty */
    bool ok = true;
    while (alive()) {
        ok = queue_push(&todo, next_network_topic()) && ok;   /* next: sockets in C++ */
        ok = queue_push(&todo, viewer_request()) && ok;       /* FIFO, like a socket buffer */
        subscribe();                                          /* no retransmission needed */
    }

    printf("queued topics, first in first out:\n");
    const size_t queued = todo.count;
    while (!queue_empty(&todo)) {
        printf("  %s\n", queue_front(&todo)->name);
        queue_pop(&todo);
    }
    return (ok && queued == 6) ? 0 : 1;
}
