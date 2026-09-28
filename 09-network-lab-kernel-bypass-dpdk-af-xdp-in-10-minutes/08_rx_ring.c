/* Kernel Bypass: DPDK, AF_XDP and Low Latency Networking - slide 8: a model of the user space ring (C version of 08_rx_ring.cpp) */
/* Build: make 08_rx_ring_c */
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

typedef struct { uint32_t len, seq; unsigned char data[56]; } Packet;
typedef struct {                         /* 1024 slots of 64 bytes */
    Packet slot[1024];
    uint32_t head, tail;                 /* NIC moves head, app tail */
} RxRing;

/* C has no member functions: rx_burst takes the ring as its first parameter.
   zero copy: pointers into the ring, no system call */
static uint32_t rx_burst(RxRing *r, Packet **out, uint32_t max) {
    uint32_t n = r->head - r->tail;
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; ++i)
        out[i] = &r->slot[(r->tail + i) & 1023];   /* power of two mask */
    r->tail += n;
    return n;                            /* 0 means: poll again */
}

_Static_assert(sizeof(Packet) == 64, "one packet of the model is one cache line");

/* The model's NIC: "DMA" writes n packets at head, never past the free space, and numbers them. */
static uint32_t g_next_seq = 0;
static uint32_t nic_dma(RxRing *r, uint32_t n) {
    uint32_t free_slots = 1024u - (r->head - r->tail);
    if (n > free_slots) n = free_slots;
    for (uint32_t i = 0; i < n; ++i) {
        Packet *p = &r->slot[(r->head + i) & 1023];
        p->seq = g_next_seq++;
        p->len = 64u + (p->seq & 3u) * 100u;       /* 64, 164, 264 or 364 bytes on the wire */
        p->data[0] = (unsigned char)(p->seq & 0xffu);
    }
    r->head += n;
    return n;
}

static RxRing ring;                              /* 64 KiB: static storage, not the stack; zero initialised */

int main(void) {
    printf("sizeof(Packet) = %zu bytes, ring = %zu KiB in %zu slots\n\n",
           sizeof(Packet), sizeof ring.slot / 1024, sizeof ring.slot / sizeof(Packet));

    /* 100 packets arrive; the application asks for up to 32 at a time */
    nic_dma(&ring, 100);
    Packet *burst[32];
    uint32_t expected = 0;
    bool ok = true;
    for (int poll = 1; poll <= 5; ++poll) {
        uint32_t n = rx_burst(&ring, burst, 32);
        printf("poll %d: rx_burst returned %" PRIu32, poll, n);
        if (n > 0) printf("  (seq %" PRIu32 " to %" PRIu32 ")", burst[0]->seq, burst[n - 1]->seq);
        else printf("  (empty: poll again, no sleep)");
        printf("\n");
        for (uint32_t i = 0; i < n; ++i) ok = ok && burst[i]->seq == expected++;
    }

    /* zero copy: the pointers point into the ring itself */
    nic_dma(&ring, 1);
    rx_burst(&ring, burst, 32);
    printf("\nzero copy: burst[0] = %p, &ring.slot[100] = %p\n",
           (const void *)burst[0], (const void *)&ring.slot[100]);
    ok = ok && burst[0] == &ring.slot[100] && burst[0]->seq == expected++;

    /* a full ring: the NIC has nowhere to write, which on real hardware is a drop (rx_missed) */
    uint32_t accepted = nic_dma(&ring, 2000);
    printf("NIC offers 2000 packets to an idle application: %" PRIu32 " fit, %" PRIu32 " would be dropped\n",
           accepted, 2000 - accepted);

    /* wrap around: head and tail only grow, the mask finds the slot */
    uint64_t received = 0;
    for (int round = 0; round < 6; ++round) {
        for (uint32_t n = rx_burst(&ring, burst, 32); n > 0; n = rx_burst(&ring, burst, 32)) {
            for (uint32_t i = 0; i < n; ++i) ok = ok && burst[i]->seq == expected++;
            received += n;
        }
        nic_dma(&ring, 700);
    }
    printf("after the wrap: head = %" PRIu32 ", tail = %" PRIu32 ", slot index of tail = %" PRIu32
           ", received in order = %" PRIu64 "\n",
           ring.head, ring.tail, ring.tail & 1023, received);
    printf("%s", ok ? "every packet arrived once and in order\n" : "ERROR: a packet was lost or reordered\n");
    return ok ? 0 : 1;
}
