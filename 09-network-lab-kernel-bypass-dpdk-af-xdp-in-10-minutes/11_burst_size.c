/* Kernel Bypass: DPDK, AF_XDP and Low Latency Networking - slide 11: burst size: throughput against latency (C version of 11_burst_size.cpp) */
/* Build: make 11_burst_size_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- timing harness: the C twin of bench_ns and sink ---- */
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

static volatile uint64_t g_sink = 0;
static void sink(uint64_t x) { g_sink = g_sink + x; }   /* keeps the result alive */

typedef struct { double min_ns; double median_ns; } BenchResult;

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* warm up, repeat, keep the minimum and the median; at most 64 repeats */
static BenchResult bench_ns(void (*f)(void *), void *ctx, int warmup, int repeats) {
    double samples[64];
    if (repeats > 64) repeats = 64;
    for (int i = 0; i < warmup; ++i) f(ctx);
    for (int i = 0; i < repeats; ++i) {
        double t0 = now_ns();
        f(ctx);
        samples[i] = now_ns() - t0;
    }
    qsort(samples, (size_t)repeats, sizeof samples[0], cmp_double);
    BenchResult r = { samples[0], samples[repeats / 2] };
    return r;
}

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

/* The model's NIC: "DMA" writes n packets at head, never past the free space, and numbers them. */
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

enum { kPackets = 262144 };              /* per timed run, a multiple of 1024 */

/* Drain kPackets from the ring with a given burst size; the NIC refills a full ring when it runs dry. */
static uint64_t drain(RxRing *r, uint32_t max_burst) {
    Packet *burst[64];
    uint64_t bytes = 0;
    for (uint32_t got = 0; got < kPackets;) {
        uint32_t n = rx_burst(r, burst, max_burst);
        if (n == 0) { nic_dma(r, 1024); continue; }
        for (uint32_t i = 0; i < n; ++i) bytes += burst[i]->len;
        got += n;
    }
    return bytes;
}

/* C has no lambdas: the captured ring and burst size become a context struct */
typedef struct { RxRing *ring; uint32_t burst; } DrainCtx;
static void run_drain(void *ctx) {
    const DrainCtx *c = (const DrainCtx *)ctx;
    sink(drain(c->ring, c->burst));
}

static RxRing ring;                              /* 64 KiB: static storage, not the stack */

int main(void) {
    printf("the model: per packet = work + poll / burst, the last packet waits (burst - 1) * work\n");
    printf("burst  per packet   last waits\n");
    /* typical costs, not measurements: replace them with your own */
    const double kPollNs = 40.0;         /* fixed cost of one rx_burst */
    const double kWorkNs = 50.0;         /* handling one packet */
    static const int bursts[] = {1, 4, 8, 16, 32, 64};
    const size_t nbursts = sizeof bursts / sizeof bursts[0];
    for (size_t k = 0; k < nbursts; ++k) {
        int burst = bursts[k];
        double per_packet = kWorkNs + kPollNs / burst;  /* amortized poll */
        double last_waits = (burst - 1) * kWorkNs;      /* queue in burst */
        printf("%5d %8.1f ns %8.1f ns\n",
               burst, per_packet, last_waits);
    }
    printf("\nthroughput wants a big burst, the tail wants a small one; 32 is the usual default\n\n");

    /* the same sweep on the ring model: here the work is one add, so the poll cost is what you see */
    printf("this machine, ring model, %u packets per run\n", (unsigned)kPackets);
    printf("burst  median ns per packet\n");
    const uint64_t expected = (uint64_t)(kPackets / 4) * (64u + 164u + 264u + 364u);
    bool ok = true;
    for (size_t k = 0; k < nbursts; ++k) {
        DrainCtx c = { &ring, (uint32_t)bursts[k] };
        ok = ok && drain(&ring, c.burst) == expected;
        BenchResult r = bench_ns(run_drain, &c, 3, 21);
        printf("%5u %10.2f\n", (unsigned)c.burst, r.median_ns / (double)kPackets);
    }
    printf("%s\n", ok ? "every burst size saw the same bytes" : "ERROR: a burst size lost packets");
    return ok ? 0 : 1;
}
