/* Polling vs Interrupts: Why Low Latency Systems Poll - slide 10: p50, p99 and cpu time (C version of 10_measure.cpp) */
/* Build: make 10_measure_c */
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

#if defined(__linux__)
static double thread_cpu_ms(void) {          /* CPU time this thread really used, not wall time */
    struct timespec ts = {0};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}
#else
static double thread_cpu_ms(void) { return -1.0; }   /* needs Linux: CLOCK_THREAD_CPUTIME_ID */
#endif

typedef struct { const char *name; int64_t p50; int64_t p99; double cpu_ms; double wall_ms; } Result;
static Result g_results[2];
static int g_count = 0;

static void report(const char *name, int64_t p50, int64_t p99, double cpu_ms) {
    Result r = { name, p50, p99, cpu_ms, 0.0 };
    g_results[g_count++] = r;
}

static int cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

/* one run: the producer gets &run->sh (the first member), the consumer gets the whole run */
typedef struct { Shared sh; const char *name; } Run;

static int consumer(void *arg) {
    Run *run = arg;
    Shared *sh = &run->sh;
    const int blocking = sh->m != NULL;
    unsigned long long seen = 0;
    for (int i = 0; i < K_EVENTS; ++i) {
        if (blocking) {
            mtx_lock(sh->m);
            while (atomic_load(&sh->slot.seq) == seen) cnd_wait(sh->cv, sh->m);
        } else {
            while (atomic_load_explicit(&sh->slot.seq, memory_order_acquire) == seen) {}
        }
        ++seen;
        sh->lat[sh->n_lat++] = now_i64() - (int64_t)atomic_load(&sh->slot.sent_ns);
        atomic_fetch_add_explicit(&sh->acked, 1, memory_order_release);
        if (blocking) mtx_unlock(sh->m);
    }

    /* sort the samples once, then read the percentiles */
    qsort(sh->lat, (size_t)sh->n_lat, sizeof sh->lat[0], cmp_i64);
    const int64_t p50 = sh->lat[(size_t)(0.50 * (double)(sh->n_lat - 1))];
    const int64_t p99 = sh->lat[(size_t)(0.99 * (double)(sh->n_lat - 1))];
    report(run->name, p50, p99, thread_cpu_ms());
    /* typical: blocking p50 tens of us, spinning p50 under 1 us */
    /* CPU time: blocking near zero, spinning equals the wall time */
    return 0;
}

int main(void) {
    static Run run;
    run.sh.lat = malloc(K_EVENTS * sizeof *run.sh.lat);
    if (run.sh.lat == NULL) { printf("out of memory\n"); return 1; }

    for (int style = 0; style < 2; ++style) {
        const int blocking = style == 0;
        mtx_t m;
        cnd_t cv;
        if (mtx_init(&m, mtx_plain) != thrd_success || cnd_init(&cv) != thrd_success) {
            printf("mtx_init or cnd_init failed\n");
            free(run.sh.lat);
            return 1;
        }
        atomic_init(&run.sh.slot.seq, 0);
        atomic_init(&run.sh.slot.sent_ns, 0);
        atomic_init(&run.sh.acked, 0);
        run.sh.m = blocking ? &m : NULL;
        run.sh.cv = blocking ? &cv : NULL;
        run.sh.n_lat = 0;
        run.name = blocking ? "blocking" : "spinning";

        const int64_t t0 = now_i64();
        thrd_t prod, cons;
        if (thrd_create(&prod, producer, &run.sh) != thrd_success) { printf("thrd_create failed\n"); return 1; }
        if (thrd_create(&cons, consumer, &run) != thrd_success) { printf("thrd_create failed\n"); return 1; }
        thrd_join(prod, NULL);
        thrd_join(cons, NULL);
        g_results[style].wall_ms = (double)(now_i64() - t0) / 1e6;
        /* C has no destructor: the mutex and the condition variable are released by hand */
        cnd_destroy(&cv);
        mtx_destroy(&m);
    }
    free(run.sh.lat);

    printf("consumer   p50 ns     p99 ns     CPU ms     wall ms   (this machine)\n");
    for (int i = 0; i < 2; ++i) {
        const Result *r = &g_results[i];
        printf("%s   %" PRId64 "        %" PRId64 "        ", r->name, r->p50, r->p99);
        if (r->cpu_ms < 0.0) printf("n/a");
        else printf("%g", r->cpu_ms);
        printf("        %g\n", r->wall_ms);
    }
    if (g_results[1].p50 > 0)
        printf("p50 ratio, blocking over spinning: %gx on this machine\n",
               (double)g_results[0].p50 / (double)g_results[1].p50);
    if (g_results[0].cpu_ms < 0.0)
        printf("CPU time of one thread needs Linux (CLOCK_THREAD_CPUTIME_ID): there the blocking consumer\n"
               "shows almost none and the spinning consumer shows the whole wall time.\n");
    else
        printf("Read the CPU column: blocking used almost none, spinning used the whole wall time.\n");
    printf("A virtual machine or a busy desktop stretches the tail: take numbers on quiet Linux hardware.\n");
    return g_count == 2 ? 0 : 1;
}
