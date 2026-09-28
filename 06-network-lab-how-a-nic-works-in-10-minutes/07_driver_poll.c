/* How a NIC Works: RX, TX, DMA and Interrupts - slide 7: the driver: a tiny irq handler, then napi poll (C version of 07_driver_poll.cpp) */
/* Build: make 07_driver_poll_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* A model of the driver side of one RX queue. No device is touched. */
typedef struct {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t status;
} RxDescriptor;

typedef struct {
    bool masked;
    bool pending;
    unsigned fired;
} MsixVector;

typedef struct {
    RxDescriptor *desc;          /* C has no std::vector: calloc plus size, freed in main */
    size_t next;                 /* next descriptor the driver will look at */
    size_t size;
    MsixVector vector;
    size_t nic_next;             /* next descriptor the NIC will fill */
    long long delivered_bytes;
    long long delivered_packets;
} RxQueue;

enum { kDone = 1u << 0 };

/* the sk_buff points at the DMA buffer: the payload is not copied here */
static void deliver_to_stack(RxQueue *q, const RxDescriptor *d) {
    q->delivered_bytes += d->length;
    ++q->delivered_packets;
}

/* Hard IRQ: mask the vector, schedule the poll, return. Tiny. */
static void irq_handler(MsixVector *v) { v->masked = true; v->pending = false; }
static int napi_poll(RxQueue *q, int budget) {   /* softirq: 0 copies */
    int done = 0;
    for (; done < budget && (q->desc[q->next].status & kDone); ++done) {
        deliver_to_stack(q, &q->desc[q->next]);  /* sk_buff wraps it */
        q->desc[q->next].status = 0;             /* buffer posted again */
        q->next = (q->next + 1) % q->size;
    }
    if (done < budget) q->vector.masked = false;   /* empty: IRQ on */
    return done;
}

/* NIC side: complete n packets (slide 6), raise the interrupt only when the vector is not masked */
static void nic_receives(RxQueue *q, int n, uint16_t len) {
    for (int i = 0; i < n; ++i) {
        RxDescriptor *d = &q->desc[q->nic_next];
        d->length = len;
        d->status = kDone;
        q->nic_next = (q->nic_next + 1) % q->size;
        if (!q->vector.masked && !q->vector.pending) { q->vector.pending = true; ++q->vector.fired; }
    }
}

int main(void) {
    RxQueue q;
    q.size = 256;
    q.desc = calloc(q.size, sizeof *q.desc);
    if (q.desc == NULL) { printf("out of memory\n"); return 1; }
    q.next = 0;
    q.vector = (MsixVector){false, false, 0};
    q.nic_next = 0;
    q.delivered_bytes = 0;
    q.delivered_packets = 0;

    printf("a burst of 150 packets lands in a ring of %zu descriptors\n", q.size);
    nic_receives(&q, 150, 1500);
    printf("  interrupts fired: %u\n", q.vector.fired);

    irq_handler(&q.vector);
    printf("hard IRQ: vector masked, poll scheduled\n");

    int round = 0;
    while (q.vector.masked) {
        int done = napi_poll(&q, 64);
        ++round;
        printf("  NAPI poll round %d: %d packets%s\n", round, done,
               q.vector.masked ? "  (budget used up: poll again, IRQ stays off)" : "  (under budget: IRQ back on)");
        if (round == 1) nic_receives(&q, 20, 1500);   /* more packets arrive while the driver polls: no interrupt */
    }

    printf("delivered %lld packets, %lld bytes, with %u interrupt and 0 payload copies in the driver\n",
           q.delivered_packets, q.delivered_bytes, q.vector.fired);
    printf("under load the driver never unmasks: it is polling, and interrupts stop completely\n");

    /* correctness: nothing lost, one interrupt for the whole burst */
    int ok = q.delivered_packets == 170 && q.vector.fired == 1 && q.next == q.nic_next;
    free(q.desc);   /* C has no destructor: the owner frees the ring */
    return ok ? 0 : 1;
}
