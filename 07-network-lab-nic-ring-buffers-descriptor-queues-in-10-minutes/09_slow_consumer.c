/* NIC Ring Buffers and Descriptor Queues Explained - slide 9: a slow consumer: counting drops (C version of 09_slow_consumer.cpp) */
/* Build: make 09_slow_consumer_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <inttypes.h>
#include <stdbool.h>
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

static bool nic_receive(RxRing *r, uint16_t len) {   /* slide 7 */
    if (r->head == r->tail) {
        ++r->rx_missed;
        return false;
    }
    RxDescriptor *d = &r->desc[r->head & r->mask];
    d->length = len;
    d->status = kDD;
    ++r->head;
    return true;
}

static uint64_t g_bytes = 0;
static void deliver(uint64_t buffer_addr, uint16_t length) { (void)buffer_addr; g_bytes += length; }

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

/* the loop of the slide with its three numbers as parameters, so the viewer can change them */
typedef struct { uint64_t arrived, delivered, missed; } Outcome;
static Outcome run(uint32_t ring_size, int burst, uint32_t budget) {
    RxRing ring;
    Outcome o = {0, 0, 0};
    if (!ring_init(&ring, ring_size)) {
        printf("out of memory\n");
        exit(1);
    }
    for (int tick = 0; tick < 1000; ++tick) {
        int arriving = (tick % 10 == 0) ? burst : 2;
        for (int i = 0; i < arriving; ++i) nic_receive(&ring, 64);
        o.arrived += (uint64_t)arriving;
        o.delivered += driver_poll(&ring, budget);
    }
    for (uint32_t n = driver_poll(&ring, budget); n != 0; n = driver_poll(&ring, budget)) o.delivered += n;
    o.missed = ring.rx_missed;
    ring_free(&ring);
    return o;
}

static void print_row(const char *what, Outcome o) {
    printf("  %s: arrived %" PRIu64 ", delivered %" PRIu64 ", rx_missed %" PRIu64 " (%g percent)\n",
           what, o.arrived, o.delivered, o.missed, 100.0 * (double)o.missed / (double)o.arrived);
}

int main(void) {
    /* each tick: packets arrive, then the driver gets its budget */
    RxRing ring;
    if (!ring_init(&ring, 8)) return 1;
    uint64_t delivered = 0;
    for (int tick = 0; tick < 1000; ++tick) {
        int arriving = (tick % 10 == 0) ? 12 : 2;  /* burst every 10 */
        for (int i = 0; i < arriving; ++i) nic_receive(&ring, 64);
        delivered += driver_poll(&ring, 3);        /* slow: 3 per tick */
    }
    /* average in: 3 per tick, the same as the budget, and still */
    /* drops: a burst of 12 does not fit in 8 descriptors */

    uint64_t arrived = 100 * 12 + 900 * 2;
    uint64_t waiting = ring.head - ring.clean;
    printf("the slide: ring 8, burst 12 every tenth tick, budget 3 per tick\n");
    printf("  arrived %" PRIu64 " (average %g per tick), delivered %" PRIu64 ", still in the ring %" PRIu64
           ", rx_missed %" PRIu64 "\n",
           arrived, (double)arrived / 1000.0, delivered, waiting, ring.rx_missed);
    bool balanced = arrived == delivered + waiting + ring.rx_missed;     /* every packet is accounted for */

    printf("change one number at a time:\n");
    Outcome base = run(8, 12, 3);
    print_row("ring 8,  burst 12, budget 3", base);
    print_row("ring 16, burst 12, budget 3", run(16, 12, 3));
    print_row("ring 8,  burst 12, budget 6", run(8, 12, 6));
    print_row("ring 8,  burst 30, budget 6", run(8, 30, 6));
    print_row("ring 32, burst 30, budget 6", run(32, 30, 6));
    printf("the average rate never exceeded the budget: drops come from the burst against the free descriptors\n");
    printf("bytes delivered to the stack of the model: %" PRIu64 "\n", g_bytes);
    bool ok = balanced && base.missed == ring.rx_missed && ring.rx_missed > 0;
    ring_free(&ring);
    return ok ? 0 : 1;
}
