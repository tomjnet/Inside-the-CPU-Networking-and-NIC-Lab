/* IP, TCP and UDP: What Actually Happens to a Packet - slide 10: nagle, tcp_nodelay and head of line blocking (C version of 10_nodelay.cpp) */
/* Build: make 10_nodelay_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stdio.h>

#if defined(__linux__)
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* A model of Nagle's rule, portable: a small write leaves at once only when nothing is waiting for an ACK;
   otherwise it is held and merged until the ACK of the previous data arrives. */
typedef struct {
    int packets;
    int worst_wait_ms;
    int left_over;
} NagleResult;

static NagleResult simulate(bool nodelay, int writes, int gap_ms, int rtt_ms) {
    NagleResult r = {0, 0, 0};
    int ack_at = -1;        /* when the data in flight gets acknowledged, -1 = nothing in flight */
    int buffered = 0;       /* bytes written by the application and not sent yet */
    int oldest = 0;         /* time of the oldest write that is still held */
    const int last = (writes - 1) * gap_ms;
    for (int t = 0; t <= last + 2 * rtt_ms; ++t) {
        if (t == ack_at) ack_at = -1;
        if (t <= last && t % gap_ms == 0) {               /* the application writes 20 bytes */
            if (buffered == 0) oldest = t;
            buffered += 20;
        }
        if (buffered > 0 && (nodelay || ack_at < 0)) {    /* allowed to send: one packet with all that is held */
            ++r.packets;
            if (t - oldest > r.worst_wait_ms) r.worst_wait_ms = t - oldest;
            buffered = 0;
            ack_at = t + rtt_ms;
        }
    }
    r.left_over = buffered;
    return r;
}

int main(void) {
    const int writes = 10, gap_ms = 1, rtt_ms = 10;
    const NagleResult nagle = simulate(false, writes, gap_ms, rtt_ms);
    const NagleResult nodelay = simulate(true, writes, gap_ms, rtt_ms);
    printf("model: 10 writes of 20 bytes, one per millisecond, round trip time 10 ms\n");
    printf("  Nagle on:     %2d packets, a write waits up to %d ms in the kernel\n", nagle.packets,
           nagle.worst_wait_ms);
    printf("  TCP_NODELAY:  %2d packets, a write waits up to %d ms\n", nodelay.packets, nodelay.worst_wait_ms);
    printf("  with delayed ACKs on the other side the wait can grow to about 40 ms (typical)\n");
    const bool model_ok = nagle.left_over == 0 && nodelay.left_over == 0 && nodelay.packets == writes &&
                          nagle.packets < nodelay.packets;

#if defined(__linux__)
    /* Nagle: a small write waits for the ACK of the previous one */
    /* TCP_NODELAY turns it off: every send() leaves now, more packets */
    int fd = socket(AF_INET, SOCK_STREAM, 0);       /* a TCP socket */
    if (fd < 0) {
        printf("socket: %s (a TCP socket would show TCP_NODELAY going from 0 to 1)\n", strerror(errno));
        return model_ok ? 0 : 1;
    }
    int before = -1;
    socklen_t before_len = sizeof before;
    if (getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &before, &before_len) != 0)
        printf("getsockopt: %s\n", strerror(errno));
    int one = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one) != 0)
        printf("setsockopt: %s\n", strerror(errno));
    int got = 0;
    socklen_t len = sizeof got;
    if (getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &got, &len) != 0)  /* reads 1 */
        printf("getsockopt: %s\n", strerror(errno));
    close(fd);                                      /* 4 system calls; C has no destructor, so close by hand */
    printf("real socket: TCP_NODELAY was %d (Nagle on by default), now %d (Nagle off)\n", before, got != 0 ? 1 : 0);
    printf("the option is per socket: set it once, right after socket(), before the first send()\n");
#else
    printf("this sample needs Linux: a real TCP socket whose TCP_NODELAY option goes from 0 to 1\n");
#endif
    return model_ok ? 0 : 1;
}
