/* Sockets in C++: Your First Network Program - slide 11: round trip time over loopback: p50 and p99 (C version of 11_rtt.cpp) */
/* Build: make 11_rtt_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>

#if defined(__linux__)
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <threads.h>        /* C11 threads; the thread code is Linux only, where glibc provides them */
#include <time.h>
#include <unistd.h>

static struct sockaddr *sa(struct sockaddr_in *a) { return (struct sockaddr *)a; }

/* C has no exceptions: -1 and errno become an Error the caller tests (slide 6) */
typedef struct { int code; char text[160]; } Error;
static long check(long rc, const char *what, Error *e) {
    if (rc < 0) {
        e->code = errno;
        snprintf(e->text, sizeof e->text, "%s: %s", what, strerror(e->code));
    }
    return rc;
}

static int recv_all(int fd, char *p, size_t want, Error *e) {  /* the receive loop of slide 9 */
    while (want > 0) {
        long n = check(recv(fd, p, want, 0), "recv", e);
        if (n < 0) return -1;
        if (n == 0) {
            snprintf(e->text, sizeof e->text, "peer closed");
            return -1;
        }
        p += n;
        want -= (size_t)n;
    }
    return 0;
}

/* a blocking call that waits longer than this fails with EAGAIN: the demo can never hang */
static long give_up_after(int fd, int seconds, Error *e) {
    struct timeval tv;
    memset(&tv, 0, sizeof tv);
    tv.tv_sec = seconds;
    return check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv), "setsockopt", e);
}

static long no_delay(int fd, Error *e) {                  /* slide 10 */
    int one = 1;
    return check(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one), "setsockopt", e);
}

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double us(double ns) { return ns / 1000.0; }

typedef struct { int lis; Error err; } Server;

/* the echo server of slide 8 in its own thread: it sleeps in recv until the client sends */
static int server(void *arg) {
    Server *s = arg;
    Error *e = &s->err;
    char buf[4096];
    int conn = (int)check(accept(s->lis, NULL, NULL), "accept", e);
    if (conn < 0) return 1;
    if (give_up_after(conn, 5, e) < 0) goto done;
    if (no_delay(conn, e) < 0) goto done;
    for (;;) {
        long n = check(recv(conn, buf, sizeof buf, 0), "recv", e);
        if (n <= 0) break;
        if (check(send(conn, buf, (size_t)n, MSG_NOSIGNAL), "send", e) < 0) break;
    }
done:
    close(conn);                /* C has no destructor: one close at the one cleanup label */
    return e->text[0] != '\0';
}
#endif

int main(void) {
#if defined(__linux__)
    enum { kTrips = 20000 };
    Error e;
    Server srv;
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    thrd_t t;
    int lis = -1, rc = 1;
    memset(&e, 0, sizeof e);
    memset(&srv, 0, sizeof srv);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);                           /* the kernel picks the port */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    lis = (int)check(socket(AF_INET, SOCK_STREAM, 0), "socket", &e);
    if (lis < 0) goto setup_failed;
    if (check(bind(lis, sa(&addr), sizeof addr), "bind", &e) < 0) goto setup_failed;
    if (check(listen(lis, 16), "listen", &e) < 0) goto setup_failed;
    if (check(getsockname(lis, sa(&addr), &alen), "getsockname", &e) < 0) goto setup_failed;
    if (give_up_after(lis, 5, &e) < 0) goto setup_failed;
    srv.lis = lis;
    if (thrd_create(&t, server, &srv) != thrd_success) {
        snprintf(e.text, sizeof e.text, "thrd_create failed");
        goto setup_failed;
    }

    int ok = 0;
    {
        Error ce;
        char msg[64];
        char reply[64];
        double *rtt_ns = NULL;                      /* one sample per ping */
        memset(&ce, 0, sizeof ce);
        memset(reply, 0, sizeof reply);
        int cli = (int)check(socket(AF_INET, SOCK_STREAM, 0), "socket", &ce);
        if (cli < 0) goto client_done;
        if (give_up_after(cli, 5, &ce) < 0) goto client_done;
        if (check(connect(cli, sa(&addr), sizeof addr), "connect", &ce) < 0) goto client_done;
        if (no_delay(cli, &ce) < 0) goto client_done;

        for (size_t i = 0; i < sizeof msg; ++i) msg[i] = (char)('a' + i % 26);
        for (int i = 0; i < 1000; ++i) {            /* warm up: caches, branch predictor, both threads awake */
            if (check(send(cli, msg, 64, MSG_NOSIGNAL), "send", &ce) < 0) goto client_done;
            if (recv_all(cli, reply, 64, &ce) < 0) goto client_done;
        }

        rtt_ns = malloc(kTrips * sizeof *rtt_ns);   /* (added) no allocation inside the timed loop */
        if (rtt_ns == NULL) {
            snprintf(ce.text, sizeof ce.text, "out of memory");
            goto client_done;
        }
        for (int i = 0; i < kTrips; ++i) {
            double t0 = now_ns();
            if (check(send(cli, msg, 64, MSG_NOSIGNAL), "send", &ce) < 0) goto client_done;
            if (recv_all(cli, reply, 64, &ce) < 0) goto client_done;   /* 4 system calls per trip */
            double t1 = now_ns();
            rtt_ns[i] = t1 - t0;
        }
        qsort(rtt_ns, kTrips, sizeof rtt_ns[0], cmp_double);   /* p50 [n/2], p99 [n*99/100] */

        const size_t n = kTrips;
        double sum = 0;
        for (size_t i = 0; i < n; ++i) sum += rtt_ns[i];
        printf("%zu round trips of 64 bytes over 127.0.0.1, TCP_NODELAY on, this machine:\n", n);
        printf("  min     %g us\n", us(rtt_ns[0]));
        printf("  p50     %g us\n", us(rtt_ns[n / 2]));
        printf("  p99     %g us\n", us(rtt_ns[n * 99 / 100]));
        printf("  p99.9   %g us\n", us(rtt_ns[n * 999 / 1000]));
        printf("  max     %g us\n", us(rtt_ns[n - 1]));
        printf("  average %g us  (hides the tail: report p50 and p99)\n", us(sum / (double)n));
        printf("  p99 / p50 = %g\n", rtt_ns[n * 99 / 100] / rtt_ns[n / 2]);
        printf("no wire and no NIC: this is 4 system calls, 4 copies and 2 thread wake ups per trip.\n"
               "Run it on real Linux hardware: a virtual machine or WSL adds its own jitter.\n");
        ok = memcmp(msg, reply, sizeof msg) == 0;   /* the last echo is byte for byte the message */
    client_done:
        if (ce.text[0] != '\0') {
            printf("client: %s\n", ce.text);
            ok = 0;
        }
        free(rtt_ns);
        if (cli >= 0) close(cli);                   /* cli closes: the server's recv returns 0 */
    }
    thrd_join(t, NULL);

    if (srv.err.text[0] != '\0') printf("server: %s\n", srv.err.text);
    rc = ok && srv.err.text[0] == '\0' ? 0 : 1;
    goto done;

setup_failed:
    printf("setup failed: %s\n", e.text);
done:
    if (lis >= 0) close(lis);
    return rc;
#else
    printf("this sample needs Linux: 20000 TCP round trips over 127.0.0.1 between two threads, "
           "then p50, p99 and p99.9 of the round trip time\n");
    return 0;
#endif
}
