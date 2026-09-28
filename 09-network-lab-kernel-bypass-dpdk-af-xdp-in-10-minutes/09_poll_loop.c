/* Kernel Bypass: DPDK, AF_XDP and Low Latency Networking - slide 9: the poll mode loop (C version of 09_poll_loop.cpp) */
/* Build: make 09_poll_loop_c */
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

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double now_ns(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e9 / (double)f.QuadPart;
}
#else
#include <time.h>
static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
#endif

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

/* The model's NIC: "DMA" writes n packets at head, never past the free space, and numbers them.
   On real hardware this runs in parallel with the loop; here it runs when the ring is dry, so the
   output is the same on every machine. */
static uint32_t g_next_seq = 0;
static void nic_dma(RxRing *r, uint32_t n) {
    uint32_t free_slots = 1024u - (r->head - r->tail);
    if (n > free_slots) n = free_slots;
    for (uint32_t i = 0; i < n; ++i) {
        Packet *p = &r->slot[(r->head + i) & 1023];
        p->seq = g_next_seq++;
        p->len = 64u + (p->seq & 3u) * 100u;       /* 64, 164, 264 or 364 bytes on the wire */
    }
    r->head += n;
}

/* The application's work on one packet, in place: check the order, account the bytes. */
static uint32_t g_expected_seq = 0;
static uint64_t g_out_of_order = 0;
static uint64_t handle(const Packet *p) {
    if (p->seq != g_expected_seq) ++g_out_of_order;
    ++g_expected_seq;
    return p->len;
}

static RxRing ring;                              /* 64 KiB: static storage, not the stack */

int main(void) {
    const uint64_t total = 1000000;
    double t0 = now_ns();

    /* the poll mode loop: one pinned core, never sleeps, never traps */
    Packet *burst[32];
    uint64_t packets = 0, bytes = 0, empty_polls = 0;
    while (packets < total) {
        uint32_t n = rx_burst(&ring, burst, 32);  /* up to 32 pointers */
        if (n == 0) {                  /* nothing yet: no sleep, ask again */
            ++empty_polls;
            nic_dma(&ring, 256);       /* model: the NIC delivers 256 more */
        }
        for (uint32_t i = 0; i < n; ++i) bytes += handle(burst[i]);
        packets += n;
    }

    double ns = now_ns() - t0;

    /* 1,000,000 packets, a quarter of each length: 64, 164, 264, 364 bytes */
    const uint64_t expected_bytes = (total / 4) * (64u + 164u + 264u + 364u);
    printf("packets     %" PRIu64 "\n", packets);
    printf("bytes       %" PRIu64 " (expected %" PRIu64 ")\n", bytes, expected_bytes);
    printf("empty polls %" PRIu64 " (one for every 256 packets in this model)\n", empty_polls);
    printf("full polls  %" PRIu64 " of 32 packets each\n", packets / 32);
    printf("system calls, sleeps and copies in the loop: 0\n");
    printf("this machine: %g ns per packet, NIC model included\n", ns / (double)packets);
    printf("on a real link the empty polls are the price: the core spins at 100 percent while it waits\n");

    bool ok = packets == total && bytes == expected_bytes && g_out_of_order == 0;
    printf("%s", ok ? "every packet handled once and in order\n" : "ERROR: a packet was lost or reordered\n");
    return ok ? 0 : 1;
}
