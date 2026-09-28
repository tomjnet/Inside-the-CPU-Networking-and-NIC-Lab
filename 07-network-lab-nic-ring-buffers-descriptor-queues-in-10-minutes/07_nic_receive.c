/* NIC Ring Buffers and Descriptor Queues Explained - slide 7: the nic side: fill or drop (C version of 07_nic_receive.cpp) */
/* Build: make 07_nic_receive_c */
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

static bool nic_receive(RxRing *r, uint16_t len) {
    if (r->head == r->tail) {      /* no posted buffer left */
        ++r->rx_missed;            /* the packet is gone: a drop */
        return false;
    }
    RxDescriptor *d = &r->desc[r->head & r->mask];
    d->length = len;               /* after the DMA to d->buffer_addr */
    d->status = kDD;               /* last: the driver may read it */
    ++r->head;                     /* only the NIC writes head */
    return true;                   /* cost: zero CPU instructions */
}

int main(void) {
    RxRing ring;
    if (!ring_init(&ring, 8)) return 1;
    for (uint32_t i = 0; i < 8; ++i) ring.desc[i].buffer_addr = 0x10000000u + i * 2048u;   /* model addresses */

    /* ten packets arrive and nobody cleans the ring: the driver is away */
    int accepted = 0;
    for (int k = 1; k <= 10; ++k) {
        uint16_t len = (uint16_t)(60 + k);
        bool ok = nic_receive(&ring, len);
        accepted += ok ? 1 : 0;
        printf("packet %d (%u bytes): %s  head=%u tail=%u rx_missed=%" PRIu64 "\n",
               k, (unsigned)len, ok ? "stored" : "DROPPED, no free descriptor",
               (unsigned)ring.head, (unsigned)ring.tail, ring.rx_missed);
    }

    printf("\nthe descriptors as the driver will find them:\n");
    for (uint32_t i = 0; i < 8; ++i) {
        const RxDescriptor *d = &ring.desc[i];
        printf("  desc[%u] buffer_addr=0x%" PRIx64 " length=%u DD=%d\n",
               (unsigned)i, d->buffer_addr, (unsigned)d->length, (int)(d->status & kDD));
    }
    printf("accepted %d, missed %" PRIu64 ": the two drops left no trace except the counter\n",
           accepted, ring.rx_missed);
    bool ok = accepted == 8 && ring.rx_missed == 2;
    ring_free(&ring);
    return ok ? 0 : 1;
}
