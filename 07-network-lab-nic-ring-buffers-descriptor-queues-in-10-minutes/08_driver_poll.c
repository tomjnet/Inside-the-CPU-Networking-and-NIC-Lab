/* NIC Ring Buffers and Descriptor Queues Explained - slide 8: the driver side: clean and refill (C version of 08_driver_poll.cpp) */
/* Build: make 08_driver_poll_c */
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

/* the network stack of the model: it only counts what it was given */
static uint64_t g_packets = 0, g_bytes = 0;
static void deliver(uint64_t buffer_addr, uint16_t length) {
    ++g_packets;
    g_bytes += length;
    printf("    delivered %u bytes from buffer 0x%" PRIx64 "\n", (unsigned)length, buffer_addr);
}

/* driver: read finished packets, hand the buffers back */
static uint32_t driver_poll(RxRing *r, uint32_t budget) {
    uint32_t done = 0;
    while (done < budget &&
           (r->desc[r->clean & r->mask].status & kDD)) {
        RxDescriptor *d = &r->desc[r->clean & r->mask];
        deliver(d->buffer_addr, d->length);   /* up the stack */
        d->status = 0;                        /* an empty buffer again */
        ++r->clean; ++r->tail; ++done;        /* only the driver writes */
    }
    return done;      /* no PCIe read: the status lives in RAM */
}

int main(void) {
    RxRing ring;
    if (!ring_init(&ring, 8)) return 1;
    for (uint32_t i = 0; i < 8; ++i) ring.desc[i].buffer_addr = 0x10000000u + i * 2048u;   /* model addresses */

    for (int k = 1; k <= 5; ++k) nic_receive(&ring, (uint16_t)(100 * k));
    printf("5 packets arrived: head=%u tail=%u clean=%u\n",
           (unsigned)ring.head, (unsigned)ring.tail, (unsigned)ring.clean);

    uint32_t polls = 0, total = 0;
    for (;;) {
        printf("  driver_poll(budget 3):\n");
        uint32_t n = driver_poll(&ring, 3);
        ++polls;
        total += n;
        printf("  cleaned %u: head=%u tail=%u clean=%u free buffers=%u\n", (unsigned)n,
               (unsigned)ring.head, (unsigned)ring.tail, (unsigned)ring.clean, (unsigned)(ring.tail - ring.head));
        if (n < 3) break;          /* fewer than the budget: the ring is empty, which is how NAPI decides to stop */
    }

    /* 7 more packets: the ring wraps, the driver keeps up, nothing is missed */
    for (int k = 6; k <= 12; ++k) nic_receive(&ring, 64);
    printf("7 more arrived, the indexes wrapped: head & mask=%u\n", (unsigned)(ring.head & ring.mask));
    printf("  driver_poll(budget 64):\n");
    total += driver_poll(&ring, 64);
    ++polls;
    printf("packets delivered %" PRIu64 ", bytes %" PRIu64 ", rx_missed %" PRIu64 "\n",
           g_packets, g_bytes, ring.rx_missed);
    printf("tail register writes: %u if the driver rings the doorbell per packet, %u"
           " if it writes once per poll: real drivers batch, because each write crosses PCIe\n",
           (unsigned)total, (unsigned)polls);
    bool ok = g_packets == 12 && ring.rx_missed == 0 && ring.head == ring.clean && ring.tail - ring.head == 8;
    ring_free(&ring);
    return ok ? 0 : 1;
}
