/* Polling vs Interrupts: Why Low Latency Systems Poll - slide 7: in the lab: a lock free slot (C version of 07_slot.cpp) */
/* Build: make 07_slot_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#pragma warning(disable : 4324)      /* structure padded due to _Alignas: that padding is the point */
#endif

#include <stdalign.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- threads: C11 <threads.h>; MinGW lacks it, so map the calls onto pthreads ---- */
#if defined(__MINGW32__)
#include <pthread.h>
#include <sched.h>
typedef pthread_t thrd_t;
typedef int (*thrd_start_t)(void *);
enum { thrd_success = 0, thrd_error = 2 };
struct thrd_boot { thrd_start_t fn; void *arg; };
static void *thrd_boot_run(void *p) {
    struct thrd_boot b = *(struct thrd_boot *)p;
    free(p);
    return (void *)(intptr_t)b.fn(b.arg);
}
static int thrd_create(thrd_t *t, thrd_start_t fn, void *arg) {
    struct thrd_boot *b = malloc(sizeof *b);
    if (b == NULL) return thrd_error;
    b->fn = fn;
    b->arg = arg;
    if (pthread_create(t, NULL, thrd_boot_run, b) != 0) { free(b); return thrd_error; }
    return thrd_success;
}
static int thrd_join(thrd_t t, int *res) {
    void *r = NULL;
    if (pthread_join(t, &r) != 0) return thrd_error;
    if (res != NULL) *res = (int)(intptr_t)r;
    return thrd_success;
}
static void thrd_yield(void) { sched_yield(); }
#else
#include <threads.h>
#endif

/* ---- timing: now_ns of the canonical harness ---- */
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

/* one producer, one consumer, one cache line: a lock free slot */
typedef struct {
    _Alignas(64) atomic_ullong seq;          /* bumped on every publish */
    atomic_llong sent_ns;                    /* when the producer wrote */
} Slot;

static void publish(Slot *s) {               /* two stores, no lock */
    atomic_store_explicit(&s->sent_ns, (long long)now_ns(), memory_order_relaxed);
    atomic_fetch_add_explicit(&s->seq, 1, memory_order_release);
}
/* the consumer watches seq: a new value means a new message */

#define K_MESSAGES 1000u

typedef struct {
    Slot slot;
    atomic_ullong acked;
    unsigned long long in_order;
} Shared;

/* the consumer watches seq; when it changes, the timestamp must already be there and must not be older */
static int consumer(void *arg) {
    Shared *sh = arg;
    long long last_sent = 0;
    for (unsigned long long seen = 0; seen < K_MESSAGES; ++seen) {
        while (atomic_load_explicit(&sh->slot.seq, memory_order_acquire) == seen) {}
        const long long sent = atomic_load_explicit(&sh->slot.sent_ns, memory_order_relaxed);
        if (sent != 0 && sent >= last_sent) ++sh->in_order;
        last_sent = sent;
        atomic_store_explicit(&sh->acked, seen + 1, memory_order_release);
    }
    return 0;
}

int main(void) {
    printf("sizeof(Slot)  = %zu bytes\n", sizeof(Slot));
    printf("alignof(Slot) = %zu : the slot owns one cache line\n", (size_t)alignof(Slot));

    static Shared sh;                        /* zero initialised: seq 0, sent_ns 0, acked 0 */
    atomic_init(&sh.slot.seq, 0);
    atomic_init(&sh.slot.sent_ns, 0);
    atomic_init(&sh.acked, 0);
    sh.in_order = 0;

    thrd_t t;
    if (thrd_create(&t, consumer, &sh) != thrd_success) {
        printf("thrd_create failed\n");
        return 1;
    }
    for (unsigned long long i = 0; i < K_MESSAGES; ++i) {
        publish(&sh.slot);
        while (atomic_load_explicit(&sh.acked, memory_order_acquire) != i + 1) thrd_yield();
    }
    thrd_join(t, NULL);

    printf("published %llu messages, %llu arrived with their timestamp in place\n",
           atomic_load(&sh.slot.seq), sh.in_order);
    printf("publish writes sent_ns first and seq second, so a new seq always has its timestamp.\n");
    return sh.in_order == K_MESSAGES ? 0 : 1;
}
