/* NIC Ring Buffers and Descriptor Queues Explained - slide 10: ring size: latency against loss (C version of 10_ring_size.cpp) */
/* Build: make 10_ring_size_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {                 /* slide 3 */
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t vlan;
} RxDescriptor;
_Static_assert(sizeof(RxDescriptor) == 16, "a descriptor is 16 bytes");

enum { kDD = 1 };                /* status bit 0: descriptor done */

typedef struct {                 /* slide 6 */
    RxDescriptor *desc;
    uint32_t mask;
    uint32_t head;
    uint32_t tail;
    uint32_t clean;
    uint64_t rx_missed;
} RxRing;

/* C has no constructor or destructor: ring_init allocates, ring_free releases */
static bool ring_init(RxRing *r, uint32_t size) {
    r->desc = calloc(size, sizeof(RxDescriptor));
    r->mask = size - 1;
    r->head = 0;
    r->tail = size;
    r->clean = 0;
    r->rx_missed = 0;
    return r->desc != NULL;
}
static void ring_free(RxRing *r) { free(r->desc); r->desc = NULL; }

/* the packet buffers of the model hold one thing: the tick the packet arrived in.
   buffer_addr is the index of the buffer, so the "DMA write" is one store into this array. */
static uint64_t *g_buffers = NULL;
static uint64_t g_now = 0;          /* the current tick */
static uint64_t g_max_wait = 0;     /* longest time a delivered packet sat in the ring, in ticks */
static uint64_t g_wait_sum = 0;
static uint64_t g_delivered = 0;

static bool nic_receive(RxRing *r, uint16_t len) {   /* slide 7, plus the arrival stamp */
    if (r->head == r->tail) {
        ++r->rx_missed;
        return false;
    }
    RxDescriptor *d = &r->desc[r->head & r->mask];
    g_buffers[(size_t)d->buffer_addr] = g_now;       /* the DMA write */
    d->length = len;
    d->status = kDD;
    ++r->head;
    return true;
}

static void deliver(uint64_t buffer_addr, uint16_t length) {
    (void)length;
    uint64_t wait = g_now - g_buffers[(size_t)buffer_addr];
    if (wait > g_max_wait) g_max_wait = wait;
    g_wait_sum += wait;
    ++g_delivered;
}

static uint32_t driver_poll(RxRing *r, uint32_t budget) {   /* slide 8 */
    uint32_t done = 0;
    while (done < budget &&
           (r->desc[r->clean & r->mask].status & kDD)) {
        RxDescriptor *d = &r->desc[r->clean & r->mask];
        deliver(d->buffer_addr, d->length);
        d->status = 0;
        ++r->clean; ++r->tail; ++done;
    }
    return done;
}

typedef struct { uint64_t rx_missed, max_wait_ticks, delivered; double mean_wait_ticks; } Result;

/* 1000 ticks: 2 packets per tick, a burst every tenth tick, then the driver cleans up to its budget.
   Returns false only when memory runs out. */
static bool run_traffic(uint32_t size, int burst, uint32_t budget, Result *out) {
    RxRing ring;
    if (!ring_init(&ring, size)) return false;
    g_buffers = calloc(size, sizeof *g_buffers);
    if (g_buffers == NULL) { ring_free(&ring); return false; }
    for (uint32_t i = 0; i < size; ++i) ring.desc[i].buffer_addr = i;
    g_max_wait = 0; g_wait_sum = 0; g_delivered = 0;
    for (g_now = 0; g_now < 1000; ++g_now) {
        int arriving = (g_now % 10 == 0) ? burst : 2;
        for (int i = 0; i < arriving; ++i) nic_receive(&ring, 64);
        driver_poll(&ring, budget);
    }
    for (; ring.head != ring.clean; ++g_now) driver_poll(&ring, budget);      /* drain what is left */
    double mean = g_delivered ? (double)g_wait_sum / (double)g_delivered : 0.0;
    out->rx_missed = ring.rx_missed;
    out->max_wait_ticks = g_max_wait;
    out->delivered = g_delivered;
    out->mean_wait_ticks = mean;
    free(g_buffers);
    g_buffers = NULL;
    ring_free(&ring);
    return true;
}

static void print_row(uint32_t size, uint64_t rx_missed, uint64_t max_wait_ticks) {
    printf("  ring %4u: rx_missed %6" PRIu64 "   worst wait %3" PRIu64 " ticks\n",
           (unsigned)size, rx_missed, max_wait_ticks);
}

int main(void) {
    printf("burst of 200 every tenth tick, 2 per tick otherwise (average 21.8), budget 22 per tick\n");
    uint64_t first_missed = 0, last_missed = 0, first_wait = 0, last_wait = 0;
    /* same traffic, four ring sizes: drops against the worst wait */
    static const uint32_t sizes[] = {8u, 32u, 128u, 512u};
    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; ++k) {
        uint32_t size = sizes[k];
        Result r;
        if (!run_traffic(size, /*burst=*/200, /*budget=*/22, &r)) { printf("out of memory\n"); return 1; }
        print_row(size, r.rx_missed, r.max_wait_ticks);
        printf("             delivered %" PRIu64 ", mean wait %g ticks, %zu bytes of descriptors\n",
               r.delivered, r.mean_wait_ticks, size * sizeof(RxDescriptor));
        if (size == 8u) { first_missed = r.rx_missed; first_wait = r.max_wait_ticks; }
        last_missed = r.rx_missed; last_wait = r.max_wait_ticks;
    }
    /* small ring: drops in every burst, almost no waiting */
    /* large ring: no drops, the end of a burst waits the longest */
    /* latency against loss: size the ring for your worst burst */

    printf("a tick is one poll of the driver: on real hardware tens of microseconds to a millisecond\n");
    printf("the ring is a buffer: it turned %" PRIu64 " lost packets into a worst wait of %" PRIu64 " ticks\n",
           first_missed, last_wait);
    bool ok = first_missed > 0 && last_missed == 0 && last_wait > first_wait;   /* the trade itself, not a timing */
    return ok ? 0 : 1;
}
