/* Inside the Linux Network Stack: From Socket to NIC - slide 10: loopback round trip: the floor (C version of 10_loopback_floor.cpp) */
/* Build: make 10_loopback_floor_c */
/* */
/* One UDP socket sends a 64 byte datagram to itself over 127.0.0.1: two system calls, two copies and the */
/* protocol code, but no NIC, no hardware interrupt and no wake up. That is the floor of the kernel stack. */
/* Timings are printed for this machine and never asserted: read your own numbers on real Linux hardware. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static const struct sockaddr *addr_of(const struct sockaddr_in *a) { return (const struct sockaddr *)a; }
#endif

/* ---- timing: now_ns from the C harness ---- */
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

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void report(const char *what, double *ns, size_t n) {
    qsort(ns, n, sizeof ns[0], cmp_double);
    printf("  %-44s min %7.1f  p50 %7.1f  p99 %7.1f  max %9.1f  (ns, this machine)\n",
           what, ns[0], ns[n / 2], ns[n * 99 / 100], ns[n - 1]);
}

int main(void) {
    /* the part with no kernel in it, on every platform: two 64 byte copies. One sample is the average of a */
    /* batch of 1000 pairs, because a single pair is shorter than the resolution of the clock. */
    static unsigned char src[64];
    static unsigned char mid[64];
    static unsigned char dst[64];
    static double copy_ns[2000];                             /* C has no vector: a fixed array and its length */
    unsigned char *volatile mid_p = mid;                     /* volatile pointers: the copies cannot be deleted */
    unsigned char *volatile dst_p = dst;
    memset(src, 0x5A, sizeof src);
    unsigned long long check = 0;
    for (int i = 0; i < 2000; ++i) {
        double t0 = now_ns();
        for (int k = 0; k < 1000; ++k) {
            memcpy(mid_p, src, sizeof src);
            memcpy(dst_p, mid_p, sizeof mid);
        }
        copy_ns[i] = (now_ns() - t0) / 1000.0;
        check += dst[(size_t)i % sizeof dst];
    }
    printf("round trip of one 64 byte message (checksum %llu)\n", check);
    report("user space: 2 copies, no kernel", copy_ns, 2000);

#if defined(__linux__)
    struct sockaddr_in me;
    memset(&me, 0, sizeof me);
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);             /* 127.0.0.1 */
    me.sin_port = 0;                                         /* the kernel picks an ephemeral port */
    unsigned char msg[64];
    unsigned char buf[128];
    memset(msg, 0x5A, sizeof msg);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);       /* UDP socket, loopback */
    if (fd < 0) {
        printf("socket: %s (on Linux this prints p50 and p99 of the loopback round trip)\n", strerror(errno));
        return 0;
    }
    /* C has no destructor: every return below closes fd by hand */
    struct timeval one_second = {1, 0};                      /* a lost datagram must not hang the sample */
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &one_second, sizeof one_second);
    bind(fd, addr_of(&me), sizeof me);             /* port 0: ephemeral */
    socklen_t len = sizeof me;
    if (getsockname(fd, (struct sockaddr *)&me, &len) != 0 || me.sin_port == 0) {
        printf("bind or getsockname: %s (no loopback here: nothing to measure)\n", strerror(errno));
        close(fd);
        return 0;
    }
    printf("UDP socket bound to 127.0.0.1:%u, sending to itself\n", (unsigned)ntohs(me.sin_port));

    for (int i = 0; i < 2000; ++i) {                         /* warm up: caches, branch predictor, socket memory */
        if (sendto(fd, msg, 64, 0, addr_of(&me), sizeof me) != 64 || recv(fd, buf, sizeof buf, 0) != 64) {
            printf("loopback round trip failed: %s (nothing to measure)\n", strerror(errno));
            close(fd);
            return 0;
        }
    }

    static double rtt_ns[20000];
    memset(buf, 0, sizeof buf);                              /* so the check below proves a datagram arrived */
    for (int i = 0; i < 20000; ++i) {
        double t0 = now_ns();
        sendto(fd, msg, 64, 0, addr_of(&me), sizeof me);  /* syscall + copy */
        recv(fd, buf, sizeof buf, 0);                     /* syscall + copy */
        rtt_ns[i] = now_ns() - t0;         /* no NIC, no IRQ, no wake up */
    }
    qsort(rtt_ns, 20000, sizeof rtt_ns[0], cmp_double);
    /* p50 and p99: the floor of the stack, 2 syscalls and 2 copies */
    close(fd);

    report("loopback UDP: 2 system calls, 2 copies", rtt_ns, 20000);
    const int intact = memcmp(buf, msg, sizeof msg) == 0;
    printf("last datagram: %s\n", intact ? "payload intact" : "MISMATCH");
    printf("the difference between the two rows is the kernel: system calls, UDP, IP, the loopback device\n");
    printf("a real NIC adds the interrupt, the softirq and the wake up on top of this floor\n");
    return intact ? 0 : 1;
#else
    printf("this sample needs Linux: it would show p50 and p99 of a UDP round trip over loopback,\n"
           "typically a few microseconds: 2 system calls, 2 copies, no NIC, no interrupt, no wake up\n");
    return 0;
#endif
}
