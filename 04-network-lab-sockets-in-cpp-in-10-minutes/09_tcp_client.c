/* Sockets in C++: Your First Network Program - slide 9: tcp client: connect, send and a byte stream (C version of 09_tcp_client.cpp) */
/* Build: make 09_tcp_client_c */
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

/* TCP is a byte stream: one recv may return part of a message; 0 when all arrived, -1 on error */
static int recv_all(int fd, char *p, size_t want, Error *e) {
    while (want > 0) {                      /* one system call per turn */
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

typedef struct { int lis; Error err; } Server;

/* the echo server of slide 8 in its own thread. The first message comes back in one send; the second
   one on purpose in two pieces, 5 bytes and then 8, the way a slow or busy network may deliver it. */
static int server(void *arg) {
    Server *s = arg;
    Error *e = &s->err;
    int one = 1;
    char buf[13];
    const struct timespec pause = {0, 50L * 1000 * 1000};  /* 50 ms */
    int conn = (int)check(accept(s->lis, NULL, NULL), "accept", e);
    if (conn < 0) return 1;
    if (give_up_after(conn, 5, e) < 0) goto done;
    if (check(setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one), "setsockopt", e) < 0) goto done;
    if (recv_all(conn, buf, sizeof buf, e) < 0) goto done;
    if (check(send(conn, buf, sizeof buf, MSG_NOSIGNAL), "send", e) < 0) goto done;
    if (recv_all(conn, buf, sizeof buf, e) < 0) goto done;
    if (check(send(conn, buf, 5, MSG_NOSIGNAL), "send", e) < 0) goto done;
    thrd_sleep(&pause, NULL);
    if (check(send(conn, buf + 5, 8, MSG_NOSIGNAL), "send", e) < 0) goto done;
done:
    close(conn);                /* C has no destructor: one close at the one cleanup label */
    return e->text[0] != '\0';
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
        char reply[13];
        memset(&ce, 0, sizeof ce);

        int cli = (int)check(socket(AF_INET, SOCK_STREAM, 0), "socket", &ce);
        if (cli < 0) goto client_done;
        if (check(connect(cli, sa(&addr), sizeof addr), "connect", &ce) < 0) goto client_done;
        if (check(send(cli, "hello, socket", 13, MSG_NOSIGNAL), "send", &ce) < 0) goto client_done;
        if (recv_all(cli, reply, 13, &ce) < 0) goto client_done;   /* blocks until all 13 */

        if (give_up_after(cli, 5, &ce) < 0) goto client_done;
        printf("connected to 127.0.0.1 port %u, reply 1: '%.*s'\n",
               (unsigned)ntohs(addr.sin_port), (int)sizeof reply, reply);
        ok = memcmp(reply, "hello, socket", 13) == 0;

        /* the third classic bug: one plain recv, and the message the server sends in two pieces */
        if (check(send(cli, "hello, stream", 13, MSG_NOSIGNAL), "send", &ce) < 0) goto client_done;
        long first = check(recv(cli, reply, sizeof reply, 0), "recv", &ce);
        if (first < 0) goto client_done;
        printf("reply 2, one plain recv: %ld of 13 bytes: '%.*s' (this run)\n", first, (int)first, reply);
        const size_t have = (size_t)first;
        if (have < sizeof reply && recv_all(cli, reply + have, sizeof reply - have, &ce) < 0) goto client_done;
        printf("reply 2 after recv_all:  13 of 13 bytes: '%.*s'\n", (int)sizeof reply, reply);
        ok = ok && memcmp(reply, "hello, stream", 13) == 0;
    client_done:
        if (ce.text[0] != '\0') {
            printf("client: %s\n", ce.text);
            ok = 0;
        }
        if (cli >= 0) close(cli);               /* C has no destructor: closed by hand */
    }
    thrd_join(t, NULL);

    if (srv.err.text[0] != '\0') printf("server: %s\n", srv.err.text);
    printf("a recv that returns less than the message is normal TCP: frame your messages and loop\n");
    rc = ok && srv.err.text[0] == '\0' ? 0 : 1;
    goto done;

setup_failed:
    printf("setup failed: %s\n", e.text);
done:
    if (lis >= 0) close(lis);
    return rc;
#else
    printf("this sample needs Linux: a TCP client whose reply arrives in two pieces, "
           "read once with a plain recv and once with recv_all\n");
    return 0;
#endif
}
