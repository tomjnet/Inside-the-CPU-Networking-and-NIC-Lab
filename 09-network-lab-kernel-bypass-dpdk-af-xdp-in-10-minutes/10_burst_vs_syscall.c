/* Kernel Bypass: DPDK, AF_XDP and Low Latency Networking - slide 10: a burst against a system call per packet (C version of 10_burst_vs_syscall.cpp) */
/* Build: make 10_burst_vs_syscall_c */
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

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
/* A real kernel crossing, the cheapest there is: getppid does no work inside the kernel. */
static void kernel_crossing(void) { (void)syscall(SYS_getppid); }
static const bool kRealCrossing = true;
#else
static void kernel_crossing(void) {}
static const bool kRealCrossing = false;
#endif

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

/* kernel path model: one crossing and one copy for every packet */
static uint32_t recv_model(RxRing *r, Packet *user_buf) {
    kernel_crossing();                   /* typical 100 to 300 ns */
    if (r->head == r->tail) return 0;
    *user_buf = r->slot[r->tail++ & 1023];   /* the copy to user space */
    return user_buf->len;
}

enum { kPackets = 65536 };               /* per timed run, a multiple of 256 and of 4 */

/* The kernel way: ask for one packet at a time, each answer is a crossing plus a copy. */
static uint64_t drain_with_recv(RxRing *r) {
    Packet user_buf = {0};
    uint64_t bytes = 0;
    for (uint32_t got = 0; got < kPackets;) {
        uint32_t len = recv_model(r, &user_buf);
        if (len == 0) { nic_dma(r, 256); continue; }          /* the model's NIC delivers when the ring is dry */
        bytes += len + user_buf.data[0];
        ++got;
    }
    return bytes;
}

/* The bypass way: up to 32 pointers per poll, packets handled where the NIC wrote them. */
static uint64_t drain_with_bursts(RxRing *r) {
    Packet *burst[32];
    uint64_t bytes = 0;
    for (uint32_t got = 0; got < kPackets;) {
        uint32_t n = rx_burst(r, burst, 32);
        if (n == 0) { nic_dma(r, 256); continue; }
        for (uint32_t i = 0; i < n; ++i) bytes += burst[i]->len + burst[i]->data[0];
        got += n;
    }
    return bytes;
}

/* C has no lambdas: each timed body is a function, the ring travels in the context pointer */
static void run_recv(void *ctx) { sink(drain_with_recv((RxRing *)ctx)); }
static void run_bursts(void *ctx) { sink(drain_with_bursts((RxRing *)ctx)); }

static RxRing ring;                              /* 64 KiB: static storage, not the stack */

int main(void) {
    if (!kRealCrossing)
        printf("this sample needs Linux: the kernel crossing is a real system call there, here it costs "
               "nothing,\nso add about 100 to 300 ns per packet (typical) to the first line\n\n");

    /* correctness first: both ways must see the same bytes */
    uint64_t a = drain_with_recv(&ring);
    uint64_t b = drain_with_bursts(&ring);

    BenchResult per_packet = bench_ns(run_recv, &ring, 3, 21);
    BenchResult per_burst = bench_ns(run_bursts, &ring, 3, 21);

    const double n = (double)kPackets;
    printf("this machine, %d packets per run, ns per packet (NIC model included)\n", kPackets);
    printf("  recv model, 1 crossing + 1 copy each: min %g  median %g\n",
           per_packet.min_ns / n, per_packet.median_ns / n);
    printf("  rx_burst of 32, zero copy:            min %g  median %g\n",
           per_burst.min_ns / n, per_burst.median_ns / n);
    printf("  ratio of the medians: %gx\n\n", per_packet.median_ns / per_burst.median_ns);
    printf("crossings per run: %d against 0; copies: %d against 0\n", kPackets + kPackets / 256, kPackets);
    printf("the model is kind to the kernel: no interrupt, no protocol code, no wake up of a sleeping thread\n");

    bool ok = a == b;
    printf("%s", ok ? "both paths saw the same bytes\n" : "ERROR: the two paths disagree\n");
    return ok ? 0 : 1;
}
