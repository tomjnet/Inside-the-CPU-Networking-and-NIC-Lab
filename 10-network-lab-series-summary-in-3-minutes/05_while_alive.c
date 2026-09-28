/* Inside the CPU: What We Learned About Networking and NICs - slide 5: thank you (C version of 05_while_alive.cpp) */
/* Build: make 05_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdio.h>

/* the imaginary tomjnet.h of the slide */
typedef struct { const char *name; } Lab;
static int episodes = 0;
static int alive(void) { return episodes < 2; }               /* two labs, then the demo ends */
static Lab next_lab(void) {
    static const char *const labs[] = {"Inside the CPU: Low Latency C++ Lab", "Inside the CPU: Cache and NUMA Lab"};
    Lab lab = { labs[episodes++] };
    return lab;
}
/* C has no references: a pointer to const is the zero-copy way to pass it */
static void count_copies(const Lab *lab) { printf("next lab: %s (passed by pointer: 0 copies)\n", lab->name); }
static void subscribe(void) { printf("  subscribed to TomJNet\n"); }

/* C has no std::queue: a fixed array with head and tail indexes, like an RX ring */
#define QUEUE_CAP 4
typedef struct { Lab items[QUEUE_CAP]; size_t head; size_t tail; } LabQueue;
static void queue_push(LabQueue *q, Lab lab) { q->items[q->tail++ % QUEUE_CAP] = lab; }
static const Lab *queue_back(const LabQueue *q) { return &q->items[(q->tail - 1) % QUEUE_CAP]; }
static const Lab *queue_front(const LabQueue *q) { return &q->items[q->head % QUEUE_CAP]; }
static size_t queue_size(const LabQueue *q) { return q->tail - q->head; }

int main(void) {
    LabQueue next_labs = { { { NULL } }, 0, 0 };   /* FIFO, like an RX ring */
    while (alive()) {
        queue_push(&next_labs, next_lab());       /* Low Latency, Cache and NUMA */
        count_copies(queue_back(&next_labs));     /* and count the wake ups */
        subscribe();                              /* lifetime benefit */
    }
    const size_t queued = queue_size(&next_labs);
    printf("%zu labs queued, first out: %s\n", queued, queue_front(&next_labs)->name);
    return queued == 2 ? 0 : 1;
}
