/* Sockets in C++: Your First Network Program - slide 7: udp echo: datagrams in, datagrams out (C version of 07_udp_echo.cpp) */
/* Build: make 07_udp_echo_c */
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
    char reply[1500];           /* what came back */
    size_t reply_len;
    Error err;                  /* text[0] != 0: the client failed */
} Client;

/* the client of the slide's comment, in its own thread: one datagram out, one datagram back */
static int client(void *arg) {
    Client *c = arg;
    char reply[1500];
    long got = -1;
    int cli = (int)check(socket(AF_INET, SOCK_DGRAM, 0), "socket", &c->err);
    if (cli < 0) return 1;
    if (give_up_after(cli, 5, &c->err) < 0) goto done;
    if (check(sendto(cli, "ping", 4, 0, sa(&c->addr), sizeof c->addr), "sendto", &c->err) < 0) goto done;
    got = check(recvfrom(cli, reply, sizeof reply, 0, NULL, NULL), "recvfrom", &c->err);
    if (got < 0) goto done;
    memcpy(c->reply, reply, (size_t)got);
    c->reply_len = (size_t)got;
done:
    close(cli);                 /* C has no destructor: one close at the one cleanup label */
    return got < 0;
}
#endif

int main(void) {
#if defined(__linux__)
    Error e;
    Client c;
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    thrd_t t;
    int srv = -1, rc = 1;
    memset(&e, 0, sizeof e);
    memset(&c, 0, sizeof c);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);                           /* the kernel picks the port */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    /* server: no listen, no accept, one datagram in, one datagram out */
    srv = (int)check(socket(AF_INET, SOCK_DGRAM, 0), "socket", &e);
    if (srv < 0) goto setup_failed;
    if (check(bind(srv, sa(&addr), sizeof addr), "bind", &e) < 0) goto setup_failed;

    /* (added) read the port back, then start the client; its datagram waits in the receive buffer */
    if (check(getsockname(srv, sa(&addr), &alen), "getsockname", &e) < 0) goto setup_failed;
    if (give_up_after(srv, 5, &e) < 0) goto setup_failed;
    printf("UDP echo server on 127.0.0.1 port %u\n", (unsigned)ntohs(addr.sin_port));
    c.addr = addr;
    if (thrd_create(&t, client, &c) != thrd_success) {
        snprintf(e.text, sizeof e.text, "thrd_create failed");
        goto setup_failed;
    }

    {
        Error se;
        char buf[1500];
        struct sockaddr_in from;
        socklen_t flen = sizeof from;
        memset(&se, 0, sizeof se);
        memset(&from, 0, sizeof from);
        long n = check(recvfrom(srv, buf, sizeof buf, 0,
                                sa(&from), &flen), "recvfrom", &se);    /* blocks */
        if (n >= 0 && check(sendto(srv, buf, (size_t)n, 0, sa(&from), flen), "sendto", &se) >= 0) {
            /* client: sendto(cli, "ping", 4, 0, sa(&addr), sizeof addr);
                       recvfrom(cli, reply, sizeof reply, 0, NULL, NULL); */
            printf("server: one datagram of %ld bytes from port %u, sent back with one sendto\n",
                   n, (unsigned)ntohs(from.sin_port));
        } else {
            printf("server: %s\n", se.text);
        }
    }
    thrd_join(t, NULL);

    if (c.err.text[0] != '\0') printf("client: %s\n", c.err.text);
    printf("client: reply '%.*s' (%zu bytes: the datagram kept its boundaries)\n",
           (int)c.reply_len, c.reply, c.reply_len);
    printf("system calls for the whole exchange: 2 on the client, 2 on the server, plus setup\n");
    rc = c.reply_len == 4 && memcmp(c.reply, "ping", 4) == 0 ? 0 : 1;
    goto done;

setup_failed:
    printf("setup failed: %s\n", e.text);
done:
    if (srv >= 0) close(srv);
    return rc;
#else
    printf("this sample needs Linux: a UDP echo server and its client over 127.0.0.1, "
           "two threads in one process, reply 'ping'\n");
    return 0;
#endif
}
