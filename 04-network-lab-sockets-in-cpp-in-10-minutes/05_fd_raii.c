/* Sockets in C++: Your First Network Program - slide 5: raii: a descriptor that closes itself (C version of 05_fd_raii.cpp) */
/* Build: make 05_fd_raii_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* C has no destructor and no deleted copy: the owner is a plain struct, closing is a call you write
   on every path, and a move is a hand over that leaves -1 behind so a second close does nothing */
typedef struct { int fd; } Fd;             /* owns one descriptor */

static void fd_close(Fd *f) {               /* one system call, safe to call twice */
    if (f->fd >= 0) close(f->fd);
    f->fd = -1;
}

static Fd fd_take(Fd *from) {               /* the move: std::exchange written by hand */
    Fd to = *from;
    from->fd = -1;
    return to;
}

_Static_assert(sizeof(Fd) == sizeof(int), "and it costs nothing: the object is one int");

/* true while the number still names an open descriptor of this process */
static int is_open(int fd) { return fcntl(fd, F_GETFD) != -1 || errno != EBADF; }

/* no exception in C: the error path returns -1 and leaves through the one cleanup label */
static int fails_half_way(int *seen, const char **why) {
    int rc = 0;
    Fd s = { socket(AF_INET, SOCK_DGRAM, 0) };
    *seen = s.fd;
    *why = "error path";
    rc = -1;
    goto done;                              /* every exit passes the label below */
done:
    fd_close(&s);                           /* the close a destructor would have run */
    return rc;
}
#endif

int main(void) {
#if defined(__linux__)
    int number = -1;
    {
        Fd sock = { socket(AF_INET, SOCK_STREAM, 0) };  /* closed by hand at the end of the scope */
        number = sock.fd;
        if (number < 0) printf("socket: %s\n", strerror(errno));
        printf("socket() returned descriptor %d (0, 1 and 2 are stdin, stdout, stderr)\n", number);
        printf("open inside the scope: %s\n", is_open(number) ? "yes" : "no");

        Fd moved = fd_take(&sock);
        printf("after the move: source holds %d, target holds %d\n", sock.fd, moved.fd);
        const int bad = sock.fd != -1 || moved.fd != number;
        fd_close(&sock);                    /* holds -1: closes nothing */
        fd_close(&moved);                   /* closes the descriptor, exactly once */
        if (bad) return 1;
    }
    const int closed_by_scope = !is_open(number);
    printf("open after the scope:  %s (closed exactly once)\n", closed_by_scope ? "no" : "yes");

    int seen = -1;
    const char *why = "";
    if (fails_half_way(&seen, &why) != 0) {
        printf("error '%s' came back from the function, descriptor %d open: %s\n",
               why, seen, is_open(seen) ? "yes" : "no");
    }
    const int closed_by_cleanup = !is_open(seen);
    printf("sizeof(Fd) = %zu bytes, the same as an int\n", sizeof(Fd));
    return closed_by_scope && closed_by_cleanup ? 0 : 1;
#else
    printf("this sample needs Linux: it shows a socket descriptor closed by one close call, "
           "on the normal path, after a move and on an error path\n");
    return 0;
#endif
}
