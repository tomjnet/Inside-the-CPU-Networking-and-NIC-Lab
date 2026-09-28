/* Sockets in C++: Your First Network Program - slide 10: tcp_nodelay: switching nagle off (C version of 10_nodelay.cpp) */
/* Build: make 10_nodelay_c */
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

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

enum {
    kRounds = 25,           /* per mode; a Nagle stall is about 40 ms, so at most about 1 s */
    kHalf = 32              /* the request is written in two halves: write, write, read */
};

typedef struct { int lis; Error err; } Server;

/* the server answers only when the whole 64 byte request is in: it has nothing to send before that,
   so its ACK for the first half is a delayed ACK, and Nagle on the client waits for exactly that ACK */
static int server(void *arg) {
    Server *s = arg;
    Error *e = &s->err;
    char request[2 * kHalf];
    int conn = (int)check(accept(s->lis, NULL, NULL), "accept", e);
    if (conn < 0) return 1;
    if (give_up_after(conn, 5, e) < 0) goto done;
    for (int i = 0; i < 2 * kRounds; ++i) {
        if (recv_all(conn, request, sizeof request, e) < 0) goto done;
        if (check(send(conn, request, 8, MSG_NOSIGNAL), "send", e) < 0) goto done;
    }
done:
    close(conn);                /* C has no destructor: one close at the one cleanup label */
    return e->text[0] != '\0';
}

typedef struct { double median_ms; double max_ms; } Stats;

/* write, write, read: the pattern that Nagle punishes; 0 on success, -1 on error */
static int exchanges(int fd, Stats *out, Error *e) {
    double ms[kRounds];
    char half[kHalf];
    char reply[8];
    memset(half, 0, sizeof half);
    for (int i = 0; i < kRounds; ++i) {
        double t0 = now_ns();
        if (check(send(fd, half, sizeof half, MSG_NOSIGNAL), "send", e) < 0) return -1;  /* leaves at once: nothing is in flight */
        if (check(send(fd, half, sizeof half, MSG_NOSIGNAL), "send", e) < 0) return -1;  /* Nagle: waits for the ACK of the first */
        if (recv_all(fd, reply, sizeof reply, e) < 0) return -1;
        double t1 = now_ns();
        ms[i] = (t1 - t0) / 1e6;
    }
    qsort(ms, kRounds, sizeof ms[0], cmp_double);
    out->median_ms = ms[kRounds / 2];
    out->max_ms = ms[kRounds - 1];
    return 0;
}
#endif

int main(void) {
#if defined(__linux__)
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
        Stats nagle, nodelay;
        memset(&ce, 0, sizeof ce);
        int cli = (int)check(socket(AF_INET, SOCK_STREAM, 0), "socket", &ce);
        if (cli < 0) goto client_done;
        if (give_up_after(cli, 5, &ce) < 0) goto client_done;
        if (check(connect(cli, sa(&addr), sizeof addr), "connect", &ce) < 0) goto client_done;

        int before = -1;
        socklen_t blen = sizeof before;
        if (check(getsockopt(cli, IPPROTO_TCP, TCP_NODELAY, &before, &blen), "getsockopt", &ce) < 0)
            goto client_done;
        printf("TCP_NODELAY on a new socket: %d (Nagle is on by default)\n", before);
        if (exchanges(cli, &nagle, &ce) < 0) goto client_done;

        /* Nagle: a small send waits for the ACK of the previous one
           TCP_NODELAY: every send leaves at once, one packet per send */
        int one = 1;
        if (check(setsockopt(cli, IPPROTO_TCP, TCP_NODELAY,
                             &one, sizeof one), "setsockopt", &ce) < 0) goto client_done;

        int on = 0;
        socklen_t len = sizeof on;
        getsockopt(cli, IPPROTO_TCP, TCP_NODELAY, &on, &len);

        printf("TCP_NODELAY after setsockopt: %d\n", on);
        if (exchanges(cli, &nodelay, &ce) < 0) goto client_done;

        printf("write 32 bytes, write 32 bytes, read the reply, %d times, this machine:\n", (int)kRounds);
        printf("  Nagle on:     median %g ms, worst %g ms\n", nagle.median_ms, nagle.max_ms);
        printf("  TCP_NODELAY:  median %g ms, worst %g ms\n", nodelay.median_ms, nodelay.max_ms);
        printf("  a worst case near 40 ms with Nagle on is the delayed ACK of the other side;\n"
               "  the first exchanges of a connection are often fast, the stall shows up later\n");
        ok = before == 0 && on != 0;
    client_done:
        if (ce.text[0] != '\0') {
            printf("client: %s\n", ce.text);
            ok = 0;
        }
        if (cli >= 0) close(cli);               /* C has no destructor: closed by hand */
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
    printf("this sample needs Linux: it times a write, write, read exchange over 127.0.0.1 "
           "with Nagle on and then with TCP_NODELAY\n");
    return 0;
#endif
}
