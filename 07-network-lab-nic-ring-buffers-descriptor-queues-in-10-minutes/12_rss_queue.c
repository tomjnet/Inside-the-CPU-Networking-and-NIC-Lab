/* NIC Ring Buffers and Descriptor Queues Explained - slide 12: rss: many rings, one per core (C version of 12_rss_queue.cpp) */
/* Build: make 12_rss_queue_c */
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

typedef struct {
    uint32_t src_ip, dst_ip;
    uint16_t src_port, dst_port;
} Flow;

/* a simple integer mix, NOT the Toeplitz hash real NICs compute with a secret key;
   what matters for the slide is only that the same flow always gives the same value */
static uint32_t mix(uint32_t h) {
    h ^= h >> 16; h *= 0x7feb352du;
    h ^= h >> 15; h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
static uint32_t hash32(uint32_t src_ip, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port) {
    uint32_t ports = ((uint32_t)src_port << 16) | dst_port;
    return mix(mix(mix(src_ip) ^ dst_ip) ^ ports);
}

/* RSS: the NIC hashes the flow, the hash picks the ring */
static uint32_t rss_queue(const Flow *f, uint32_t queues) {
    uint32_t h = hash32(f->src_ip, f->dst_ip,
                        f->src_port, f->dst_port);
    return h % queues;    /* real NICs: a 128 entry lookup table */
}
/* same flow, same queue, same core: packets stay in order */
/* 8 queues on 8 cores: no lock between them, warm caches */

/* C has no std::mt19937: a fixed-seed xorshift64 gives the same flows on every run and toolchain,
   but not the same flows as the C++ version, so the per queue counts differ from its output */
static uint64_t g_rng = 7;
static uint32_t rng(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 32);
}

enum { QUEUES = 8, FLOWS = 1000 };

int main(void) {
    const uint32_t queues = QUEUES;

    /* 1000 client flows to one server address and port */
    static Flow flows[FLOWS];
    for (int i = 0; i < FLOWS; ++i) {
        flows[i].src_ip = 0x0A000000u + (rng() & 0xFFFFu);
        flows[i].dst_ip = 0x0A0000FEu;
        flows[i].src_port = (uint16_t)(1024u + (rng() % 60000u));
        flows[i].dst_port = 443;
    }

    /* 100 000 packets, each from a random flow: count per queue, and check the flow never changes queue */
    uint64_t per_queue[QUEUES] = {0};
    static uint32_t first_queue[FLOWS];
    for (int i = 0; i < FLOWS; ++i) first_queue[i] = queues;
    bool stable = true;
    for (int p = 0; p < 100000; ++p) {
        size_t i = rng() % FLOWS;
        uint32_t q = rss_queue(&flows[i], queues);
        if (first_queue[i] == queues) first_queue[i] = q;
        stable = stable && first_queue[i] == q;
        ++per_queue[q];
    }

    printf("1000 flows, 100000 packets, %u RX queues (one ring, one MSI-X vector, one core each)\n",
           (unsigned)queues);
    for (uint32_t q = 0; q < queues; ++q)
        printf("  queue %u -> core %u: %" PRIu64 " packets\n", (unsigned)q, (unsigned)q, per_queue[q]);
    printf("every flow stayed on one queue: %s (so its packets are handled in order by one core)\n",
           stable ? "yes" : "NO");

    /* the weak spot: RSS balances flows, not packets. One heavy flow cannot be spread. */
    Flow feed = {0x0A000001u, 0x0A0000FEu, 30001, 443};
    printf("one market data feed is one flow: all of it lands on queue %u, whatever the number of queues\n",
           (unsigned)rss_queue(&feed, queues));
    printf("on Linux: ethtool -x eth0 prints the real lookup table, ethtool -l eth0 the number of queues\n");
    return stable ? 0 : 1;
}
