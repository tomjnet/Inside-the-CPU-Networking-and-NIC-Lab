/* Sockets in C++: Your First Network Program - slide 6: errors: errno and one check function (C version of 06_errno.cpp) */
/* Build: make 06_errno_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static struct sockaddr *sa(struct sockaddr_in *a) { return (struct sockaddr *)a; }
#endif

/* C has no exceptions: a failed call fills an Error (the errno code plus "what: text")
   and the caller tests the return value */
typedef struct { int code; char text[160]; } Error;

/* every socket call returns -1 on failure and sets errno */
static long check(long rc, const char *what, Error *e) {
    if (rc < 0) {
        e->code = errno;
        snprintf(e->text, sizeof e->text, "%s: %s", what, strerror(e->code));
    }
    return rc;
}

static void describe(const char *name, int code) {
    printf("  %s = %d: %s\n", name, code, strerror(code));
}

int main(void) {
    /* portable part: the codes of the slide and the text strerror gives them */
    printf("errno codes a socket program meets (numbers differ between systems):\n");
    describe("ECONNREFUSED", ECONNREFUSED);
    describe("EADDRINUSE", EADDRINUSE);
    describe("EPIPE", EPIPE);
    describe("EINTR", EINTR);
    describe("EAGAIN", EAGAIN);

    Error e;
    memset(&e, 0, sizeof e);
    printf("check(7, ...) returns %ld: a good return value passes through\n", check(7, "fine", &e));
    errno = EADDRINUSE;                         /* what a failed bind would have left behind */
    if (check(-1, "bind", &e) < 0)
        printf("check(-1, \"bind\") failed: %s (code %d)\n", e.text, e.code);

#if defined(__linux__)
    /* find a loopback port where nobody listens: bind to port 0, read the port, close again */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    memset(&e, 0, sizeof e);
    {
        int probe = (int)check(socket(AF_INET, SOCK_STREAM, 0), "socket", &e);
        if (probe < 0) goto setup_failed;
        socklen_t len = sizeof addr;
        const int bad = check(bind(probe, sa(&addr), sizeof addr), "bind", &e) < 0 ||
                        check(getsockname(probe, sa(&addr), &len), "getsockname", &e) < 0;
        close(probe);                           /* C has no destructor: closed by hand */
        if (bad) goto setup_failed;
    }
    printf("connecting to 127.0.0.1 port %u, where nobody listens\n", (unsigned)ntohs(addr.sin_port));

    int refused = 0;
    int s = (int)check(socket(AF_INET, SOCK_STREAM, 0), "socket", &e);
    if (s < 0) goto setup_failed;
    Error ce;
    memset(&ce, 0, sizeof ce);
    if (check(connect(s, sa(&addr), sizeof addr), "connect", &ce) < 0) {   /* nobody listens there */
        printf("%s\n", ce.text);                /* Connection refused */
        refused = ce.code == ECONNREFUSED;
    }
    printf("%s", refused ? "the error came back as a return value with the errno code inside\n"
                         : "expected ECONNREFUSED and did not get it\n");
    close(s);
    return refused ? 0 : 1;

setup_failed:
    printf("setup failed: %s\n", e.text);
    return 1;
#else
    printf("this sample needs Linux: it connects to a closed loopback port and prints "
           "'connect: Connection refused'\n");
    return 0;
#endif
}
