/* Sockets in C++: Your First Network Program - slide 8: tcp echo server: listen, accept, recv, send (C version of 08_tcp_server.cpp) */
/* Build: make 08_tcp_server_c */
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
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <threads.h>        /* C11 threads; the thread code is Linux only, where glibc provides them */
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

/* a blocking call that waits longer than this fails with EAGAIN: the demo can never hang */
static long give_up_after(int fd, int seconds, Error *e) {
    struct timeval tv;
    memset(&tv, 0, sizeof tv);
    tv.tv_sec = seconds;
    return check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv), "setsockopt", e);
}

typedef struct {
    struct sockaddr_in addr;    /* where the server listens */
    char echoed[64];            /* every byte that came back */
    size_t echoed_len;
    Error err;                  /* text[0] != 0: the client failed */
} Client;

/* a small client in its own thread: connect, send three messages, read every byte back, close */
static int client(void *arg) {
    Client *c = arg;
    static const char *const msgs[] = {"hello, ", "echo ", "server"};
    int cli = (int)check(socket(AF_INET, SOCK_STREAM, 0), "socket", &c->err);
    if (cli < 0) return 1;
    if (give_up_after(cli, 5, &c->err) < 0) goto done;
    if (check(connect(cli, sa(&c->addr), sizeof c->addr), "connect", &c->err) < 0) goto done;
    for (size_t m = 0; m < sizeof msgs / sizeof msgs[0]; ++m) {
        const size_t size = strlen(msgs[m]);
        if (check(send(cli, msgs[m], size, MSG_NOSIGNAL), "send", &c->err) < 0) goto done;
        size_t got = 0;
        char buf[64];
        while (got < size) {
            long n = check(recv(cli, buf, sizeof buf, 0), "recv", &c->err);
            if (n <= 0) goto done;
            if (c->echoed_len + (size_t)n > sizeof c->echoed) goto done;
            memcpy(c->echoed + c->echoed_len, buf, (size_t)n);
            c->echoed_len += (size_t)n;
            got += (size_t)n;
        }
    }
done:
    close(cli);                 /* cli closes here: the server's recv returns 0 */
    return c->err.text[0] != '\0';
}
#endif

int main(void) {
#if defined(__linux__)
    Error e;
    Client c;
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    thrd_t t;
    int lis = -1, rc = 1;
    memset(&e, 0, sizeof e);
    memset(&c, 0, sizeof c);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);                           /* the kernel picks the port */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    lis = (int)check(socket(AF_INET, SOCK_STREAM, 0), "socket", &e);
    if (lis < 0) goto setup_failed;
    if (check(bind(lis, sa(&addr), sizeof addr), "bind", &e) < 0) goto setup_failed;
    if (check(listen(lis, 16), "listen", &e) < 0) goto setup_failed;   /* backlog: 16 pending */

    /* (added) read the port back, then start the client; its handshake waits in the backlog */
    if (check(getsockname(lis, sa(&addr), &alen), "getsockname", &e) < 0) goto setup_failed;
    if (give_up_after(lis, 5, &e) < 0) goto setup_failed;
    printf("TCP echo server listening on 127.0.0.1 port %u, descriptor %d\n",
           (unsigned)ntohs(addr.sin_port), lis);
    c.addr = addr;
    if (thrd_create(&t, client, &c) != thrd_success) {
        snprintf(e.text, sizeof e.text, "thrd_create failed");
        goto setup_failed;
    }

    long long calls = 0, bytes = 0;
    {
        Error se;
        memset(&se, 0, sizeof se);
        /* accept blocks, then returns a NEW descriptor for this client */
        int conn = (int)check(accept(lis, NULL, NULL), "accept", &se);
        if (conn >= 0 && give_up_after(conn, 5, &se) >= 0) {
            printf("accept returned descriptor %d: the listening one stays open\n", conn);
            char buf[4096];
            for (;;) {
                long n = check(recv(conn, buf, sizeof buf, 0), "recv", &se);
                if (n <= 0) break;                      /* 0: the peer closed */
                if (check(send(conn, buf, (size_t)n, MSG_NOSIGNAL), "send", &se) < 0) break;
                ++calls;
                bytes += n;
            }
        }
        if (se.text[0] != '\0')
            printf("server: %s\n", se.text);
        else
            printf("recv returned 0: the client closed, the loop ends, both descriptors are closed by hand\n");
        if (conn >= 0) close(conn);             /* C has no destructor: close it on the way out */
    }
    thrd_join(t, NULL);

    if (c.err.text[0] != '\0') printf("client: %s\n", c.err.text);
    printf("server echoed %lld bytes in %lld recv and send pairs\n", bytes, calls);
    printf("client got back: '%.*s'\n", (int)c.echoed_len, c.echoed);
    rc = c.echoed_len == 18 && memcmp(c.echoed, "hello, echo server", 18) == 0 ? 0 : 1;
    goto done;

setup_failed:
    printf("setup failed: %s\n", e.text);
done:
    if (lis >= 0) close(lis);
    return rc;
#else
    printf("this sample needs Linux: a TCP echo server (socket, bind, listen, accept, recv, send) "
           "and a client thread over 127.0.0.1\n");
    return 0;
#endif
}
