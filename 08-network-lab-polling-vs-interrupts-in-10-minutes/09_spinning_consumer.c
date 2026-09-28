/* Polling vs Interrupts: Why Low Latency Systems Poll - slide 9: the consumer that spins (C version of 09_spinning_consumer.cpp) */
/* Build: make 09_spinning_consumer_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#pragma warning(disable : 4324)      /* structure padded due to _Alignas: that padding is the point */
#endif

#include <inttypes.h>
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
/* the C11 mutex and condition variable, mapped the same way (inline: unused ones cost no warning) */
typedef pthread_mutex_t mtx_t;
typedef pthread_cond_t cnd_t;
enum { mtx_plain = 0 };
static inline void thrd_yield(void) { sched_yield(); }
static inline int mtx_init(mtx_t *m, int type) { (void)type; return pthread_mutex_init(m, NULL) == 0 ? thrd_success : thrd_error; }
static inline int mtx_lock(mtx_t *m) { return pthread_mutex_lock(m) == 0 ? thrd_success : thrd_error; }
static inline int mtx_unlock(mtx_t *m) { return pthread_mutex_unlock(m) == 0 ? thrd_success : thrd_error; }
static inline void mtx_destroy(mtx_t *m) { pthread_mutex_destroy(m); }
static inline int cnd_init(cnd_t *c) { return pthread_cond_init(c, NULL) == 0 ? thrd_success : thrd_error; }
static inline int cnd_signal(cnd_t *c) { return pthread_cond_signal(c) == 0 ? thrd_success : thrd_error; }
static inline int cnd_wait(cnd_t *c, mtx_t *m) { return pthread_cond_wait(c, m) == 0 ? thrd_success : thrd_error; }
static inline void cnd_destroy(cnd_t *c) { pthread_cond_destroy(c); }
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
static int64_t now_i64(void) { return (int64_t)now_ns(); }

typedef struct {
    _Alignas(64) atomic_ullong seq;          /* bumped on every publish */
    atomic_llong sent_ns;                    /* when the producer wrote */
} Slot;

static void publish(Slot *s) {               /* two stores, no lock */
    atomic_store_explicit(&s->sent_ns, (long long)now_i64(), memory_order_relaxed);
    atomic_fetch_add_explicit(&s->seq, 1, memory_order_release);
}

#define K_EVENTS 2000                        /* messages per run */
#define K_GAP_NS 100000                      /* the wire is quiet for about 100 us between messages */

/* Everything the two threads share; C has no lambda capture, so one struct travels as the void * argument. */
typedef struct {
    Slot slot;
    atomic_ullong acked;
    mtx_t *m;                                /* NULL: spinning consumer */
    cnd_t *cv;
    int64_t *lat;                            /* K_EVENTS samples */
    int n_lat;
} Shared;

/* The producer plays the NIC. It paces itself with a busy wait (a sleep would be far too coarse), publishes,
   and then waits until the consumer has taken the message, so no message is ever overwritten. */
static int producer(void *arg) {
    Shared *sh = arg;
    for (int i = 0; i < K_EVENTS; ++i) {
        const int64_t next = now_i64() + K_GAP_NS;
        while (now_i64() < next) {}
        if (sh->m != NULL) {                 /* blocking consumer: publish under the mutex, then wake it */
            mtx_lock(sh->m);
            publish(&sh->slot);
            mtx_unlock(sh->m);
            cnd_signal(sh->cv);
        } else {
            publish(&sh->slot);              /* spinning consumer: nothing else to do */
        }
        while (atomic_load_explicit(&sh->acked, memory_order_acquire) != (unsigned long long)i + 1) thrd_yield();
    }
    return 0;
}

static void record(Shared *sh, int64_t ns) { /* keep the sample, then tell the producer it may go on */
    sh->lat[sh->n_lat++] = ns;
    atomic_fetch_add_explicit(&sh->acked, 1, memory_order_release);
}

static int consumer(void *arg) {
    Shared *sh = arg;
    /* the polling style: never sleep, ask the slot again and again */
    unsigned long long seen = 0, empty_polls = 0;
    for (int i = 0; i < K_EVENTS; ++i) {
        while (atomic_load_explicit(&sh->slot.seq, memory_order_acquire) == seen)
            ++empty_polls;                   /* one L1 read, about 1 ns */
        ++seen;
        record(sh, now_i64() - (int64_t)atomic_load(&sh->slot.sent_ns));
    }
    /* producer side: publish(slot) and nothing else, no system call */
    /* cost: this core shows 100 percent busy, even with no messages */
    printf("messages seen: %llu, empty polls: %llu\n", seen, empty_polls);
    return 0;
}

static int cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static int64_t percentile(const int64_t *sorted, int n, double p) {
    return sorted[(size_t)(p * (double)(n - 1))];
}

int main(void) {
    static Shared sh;                        /* m and cv stay NULL: the producer only publishes */
    atomic_init(&sh.slot.seq, 0);
    atomic_init(&sh.slot.sent_ns, 0);
    atomic_init(&sh.acked, 0);
    sh.lat = malloc(K_EVENTS * sizeof *sh.lat);
    if (sh.lat == NULL) { printf("out of memory\n"); return 1; }

    thrd_t prod, cons;
    if (thrd_create(&prod, producer, &sh) != thrd_success) { printf("thrd_create failed\n"); free(sh.lat); return 1; }
    if (thrd_create(&cons, consumer, &sh) != thrd_success) {
        printf("thrd_create failed\n");
        return 1;    /* the producer still runs: leave its buffer alone, exiting ends the process */
    }
    thrd_join(prod, NULL);
    thrd_join(cons, NULL);

    qsort(sh.lat, (size_t)sh.n_lat, sizeof sh.lat[0], cmp_i64);
    printf("spinning consumer, wake up latency, this machine:\n");
    printf("  p50 %" PRId64 " ns\n", percentile(sh.lat, sh.n_lat, 0.50));
    printf("  p99 %" PRId64 " ns\n", percentile(sh.lat, sh.n_lat, 0.99));
    printf("  max %" PRId64 " ns\n", sh.lat[sh.n_lat - 1]);
    printf("No system call and no context switch: the price is the empty polls counter above.\n");
    const int rc = sh.n_lat == K_EVENTS ? 0 : 1;
    free(sh.lat);                            /* C has no destructor: the buffer is released by hand */
    return rc;
}
