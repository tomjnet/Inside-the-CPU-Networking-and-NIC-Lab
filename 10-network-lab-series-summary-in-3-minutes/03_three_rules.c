/* Inside the CPU: What We Learned About Networking and NICs - slide 3: three rules: copies, wake ups, local (C version of 03_three_rules.cpp) */
/* Build: make 03_three_rules_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static void sink(uint64_t x) { g_sink = g_sink + x; }   /* keeps the result alive: the loop cannot be deleted */

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

/* rule 2 as a model: typical orders of magnitude, not measurements (episodes 5 and 8 measure them) */
typedef struct { const char *name; int copies; int wakeups; double typical_ns; } Stage;

typedef struct { uint16_t len; unsigned char data[1500]; } Packet;

#define RING_SIZE 1024u                                      /* power of two: mask, no modulo */

static unsigned char app_buffer[1500];                        /* the application's buffer that recv fills */

/* C has no lambda capture: the context struct carries what the C++ lambdas captured by reference */
typedef struct {
    Packet *ring;
    unsigned char *buf;
    size_t *head;
    size_t *tail;
} Ctx;

static void copy_then_parse(void *p) {                        /* rule 1: recv copies 24 lines */
    Ctx *c = (Ctx *)p;
    for (size_t i = 0; i < RING_SIZE; ++i) {
        memcpy(c->buf, c->ring[i].data, c->ring[i].len); sink(c->buf[9]);
    }
}

static void parse_in_place(void *p) {                         /* zero copy: read it in the ring */
    Ctx *c = (Ctx *)p;
    for (*c->head += RING_SIZE; *c->tail != *c->head; ++*c->tail)   /* the NIC moved head */
        sink(c->ring[*c->tail & (RING_SIZE - 1)].data[9]);
}                                                             /* rule 2: it polls, no wake up */

/* fixed-seed splitmix64 instead of std::mt19937: different bytes, the same work on every run */
static uint64_t splitmix64(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void print_path(const char *title, const Stage *stages, size_t count) {
    int copies = 0, wakeups = 0;
    double total = 0.0;
    printf("  %s\n", title);
    for (size_t i = 0; i < count; ++i) {
        printf("    %-32s%8.1f ns\n", stages[i].name, stages[i].typical_ns);
        copies += stages[i].copies;
        wakeups += stages[i].wakeups;
        total += stages[i].typical_ns;
    }
    printf("    total %.1f ns, CPU copies %d, wake ups %d\n", total, copies, wakeups);
}

int main(void) {
    unsigned char *volatile escape = app_buffer;              /* the compiler cannot prove who else reads it */
    unsigned char *buf = escape;

    Packet *ring = malloc(RING_SIZE * sizeof *ring);          /* C has no std::vector: malloc plus a length */
    if (ring == NULL) { printf("malloc failed\n"); return 1; }
    _Alignas(64) size_t head = 0;                             /* rule 3: own cache lines */
    _Alignas(64) size_t tail = 0;

    uint64_t rng = 42;                                        /* fixed seed: every run and toolchain does the same work */
    uint64_t expected = 0;                                    /* what one pass over the ring must add up to */
    for (size_t i = 0; i < RING_SIZE; ++i) {
        ring[i].len = 1500;
        for (size_t j = 0; j < sizeof ring[i].data; ++j)
            ring[i].data[j] = (unsigned char)(splitmix64(&rng) & 0xFFu);
        expected += ring[i].data[9];                          /* byte 9 of an IPv4 header is the protocol field */
    }

    Ctx ctx = { ring, buf, &head, &tail };
    BenchResult copied = bench_ns(copy_then_parse, &ctx, 3, 21);
    BenchResult in_place = bench_ns(parse_in_place, &ctx, 3, 21);

    /* correctness, outside the timing: both readers must see the same bytes, and the ring must be drained */
    uint64_t sum_copy = 0, sum_ring = 0;
    for (size_t i = 0; i < RING_SIZE; ++i) { memcpy(buf, ring[i].data, ring[i].len); sum_copy += buf[9]; }
    for (head += RING_SIZE; tail != head; ++tail) sum_ring += ring[tail & (RING_SIZE - 1)].data[9];
    const int ok = sum_copy == expected && sum_ring == expected && tail == head;

    const double n = (double)RING_SIZE;
    printf("rule 1, count the copies (%u packets of 1500 bytes, %d cache lines each)\n",
           RING_SIZE, (1500 + 63) / 64);
    printf("  copy then parse : min %.1f ns, median %.1f ns per packet\n",
           copied.min_ns / n, copied.median_ns / n);
    printf("  parse in place  : min %.1f ns, median %.1f ns per packet\n",
           in_place.min_ns / n, in_place.median_ns / n);
    if (in_place.median_ns > 0.0)
        printf("  ratio on this machine: %.1f times (yours will differ, run it on real Linux hardware)\n",
               copied.median_ns / in_place.median_ns);
    printf("  same bytes seen by both readers: %s\n\n", ok ? "yes" : "NO");

    const Stage kernel_path[] = {
        {"NIC, DMA into kernel memory", 0, 0, 1000.0},
        {"interrupt and softirq", 0, 0, 3000.0},
        {"IP and TCP processing", 0, 0, 2000.0},
        {"wake up the blocked thread", 0, 1, 5000.0},
        {"recv: system call plus copy", 1, 0, 500.0},
    };
    const Stage bypass_path[] = {
        {"NIC, DMA into the user ring", 0, 0, 1000.0},
        {"polling core sees tail != head", 0, 0, 50.0},
    };
    printf("rule 2, count the wake ups (a model with typical orders of magnitude, not a measurement)\n");
    print_path("kernel path", kernel_path, sizeof kernel_path / sizeof kernel_path[0]);
    print_path("bypass path", bypass_path, sizeof bypass_path / sizeof bypass_path[0]);

    const uintptr_t head_at = (uintptr_t)&head;
    const uintptr_t tail_at = (uintptr_t)&tail;
    const uintptr_t apart = head_at > tail_at ? head_at - tail_at : tail_at - head_at;
    printf("\nrule 3, keep the ring and the core local\n");
    printf("  head and tail are %" PRIuPTR " bytes apart: %s", apart,
           apart >= 64 ? "each index has its own cache line\n" : "they share a cache line\n");
    printf("  on real hardware also pin the polling core to the NUMA node of the NIC (taskset, numactl)\n");
    free(ring);
    return ok && apart >= 64 ? 0 : 1;
}
