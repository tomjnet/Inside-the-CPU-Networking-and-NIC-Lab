/* How a NIC Works: RX, TX, DMA and Interrupts - slide 10: offloads: checksum, tso and rss (C version of 10_rss_queue.cpp) */
/* Build: make 10_rss_queue_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* A model of receive side scaling plus the arithmetic of TSO. The hash is a toy: real cards use the
   Toeplitz hash with a secret key and an indirection table, but the property shown here is the same. */
typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
} Flow;

/* RSS: the NIC hashes the flow and picks the RX queue (the core) */
static uint32_t rss_queue(const Flow *f, uint32_t queues) {
    uint32_t h = f->src_ip ^ (f->dst_ip * 2654435761u);
    h ^= ((uint32_t)f->src_port << 16) | f->dst_port;
    h *= 2246822519u;              /* real NICs: the Toeplitz hash */
    return (h >> 16) % queues;     /* plus an indirection table */
}   /* same flow, same queue, same core: in order, warm cache */

/* C has no std::mt19937: a fixed-seed splitmix64 (the counts differ from the C++ run) */
static uint64_t rng_state = 42;
static uint64_t splitmix64(void) {
    uint64_t z = (rng_state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

#define kQueues 4u

int main(void) {
    const uint32_t queues = kQueues;
    const uint32_t server = 0x0a000001u;   /* 10.0.0.1 */

    printf("six flows to 10.0.0.1:443, five packets each, %u RX queues:\n", queues);
    bool stable = true;
    for (uint16_t i = 0; i < 6; ++i) {
        Flow f = {0x0a000064u + i, server, (uint16_t)(40000 + 7 * i), 443};
        uint32_t first = rss_queue(&f, queues);
        for (int packet = 1; packet < 5; ++packet) stable = stable && (rss_queue(&f, queues) == first);
        printf("  flow from 10.0.0.%d:%u  -> queue %u (core %u) for every packet\n",
               100 + i, (unsigned)f.src_port, first, first);
    }

    long per_queue[kQueues] = {0};
    const int flows = 100000;
    for (int i = 0; i < flows; ++i) {
        Flow f = {(uint32_t)splitmix64(), server, (uint16_t)(splitmix64() & 0xffffu), 443};
        ++per_queue[rss_queue(&f, queues)];
    }
    printf("\n%d random flows spread over the queues:\n", flows);
    long low = flows, high = 0;
    for (uint32_t q = 0; q < queues; ++q) {
        printf("  queue %u: %ld\n", q, per_queue[q]);
        if (per_queue[q] < low) low = per_queue[q];
        if (per_queue[q] > high) high = per_queue[q];
    }
    printf("many flows balance well; one heavy flow still lands on one queue and one core\n\n");

    /* TSO: how many wire frames the card makes from one segment the stack hands over */
    const int segment = 64 * 1024;
    const int mss = 1448;   /* 1500 MTU minus IP (20), TCP (20) and timestamps (12) */
    const int frames = (segment + mss - 1) / mss;
    printf("TSO: one %d byte segment becomes %d frames of up to %d payload bytes; the stack ran once instead of %d times\n",
           segment, frames, mss, frames);
    printf("checksum offload: the result arrives as a status bit of the RX descriptor (see 06_descriptor_irq)\n");

    /* correctness: a flow never changes queue, and no queue is starved */
    return (stable && low > flows / 8 && high < flows / 2 && frames == 46) ? 0 : 1;
}
