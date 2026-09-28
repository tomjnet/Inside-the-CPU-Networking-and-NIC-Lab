/* IP, TCP and UDP: What Actually Happens to a Packet - slide 8: sequence numbers, acks and retransmission (C version of 08_retransmit.cpp) */
/* Build: make 08_retransmit_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t next;        /* the next byte of the stream it expects */
    uint32_t delivered;   /* bytes handed to the application, in order */
} Receiver;

/* receiver: the cumulative ACK is the next byte it expects (C has no references: a pointer) */
static uint32_t on_segment(Receiver *r, uint32_t seq, uint32_t len) {
    if (seq == r->next) {         /* in order: deliver to the app */
        r->next += len;
        r->delivered += len;
    }                             /* a gap: keep asking for r->next */
    return r->next;               /* the ACK number that goes back */
}
/* sender: no ACK beyond seq when the timer fires = send it again */
/* cost of one loss: at least one round trip, often a timeout */

typedef struct {
    int rounds;
    uint32_t sent;
    uint32_t delivered;
} Result;

/* A deliberately small sender: a fixed window of 4 segments per round trip, and after a loss it resends from the
   first byte that was not acknowledged. A real stack keeps the out of order segments (SACK) and resends only the hole. */
static Result transfer(bool lose_one) {
    const uint32_t first = 1000, len = 1000, segments = 8, window = 4;
    const uint32_t end = first + segments * len;
    Receiver rx = {first, 0};
    Result res = {0, 0, 0};
    uint32_t acked = first;               /* everything below this is confirmed */
    bool lost_already = !lose_one;

    while (acked < end) {
        ++res.rounds;
        printf("  round trip %d\n", res.rounds);
        uint32_t best_ack = acked;
        uint32_t dup_acks = 0;
        for (uint32_t i = 0; i < window; ++i) {
            const uint32_t seq = acked + i * len;
            if (seq >= end) break;
            res.sent += len;
            if (seq == 3000 && !lost_already) {           /* the network drops this one, once */
                lost_already = true;
                printf("    send seq=%u len=%u   LOST on the way\n", (unsigned)seq, (unsigned)len);
                continue;
            }
            const uint32_t ack = on_segment(&rx, seq, len);
            if (ack == best_ack) ++dup_acks;
            if (ack > best_ack) best_ack = ack;
            printf("    send seq=%u len=%u   ack=%u%s\n", (unsigned)seq, (unsigned)len, (unsigned)ack,
                   ack <= seq ? "   duplicate ACK: a gap, segment not delivered" : "");
        }
        if (dup_acks > 0)
            printf("    sender: ACK stuck at %u, retransmit from there\n", (unsigned)best_ack);
        acked = best_ack;
    }
    res.delivered = rx.delivered;
    return res;
}

int main(void) {
    printf("8 segments of 1000 bytes, window of 4 segments, no loss:\n");
    const Result clean = transfer(false);
    printf("the same transfer, the segment with seq=3000 is lost once:\n");
    const Result lossy = transfer(true);

    printf("no loss:  %d round trips, %u bytes sent, %u delivered in order\n", clean.rounds,
           (unsigned)clean.sent, (unsigned)clean.delivered);
    printf("one loss: %d round trips, %u bytes sent, %u delivered in order\n", lossy.rounds,
           (unsigned)lossy.sent, (unsigned)lossy.delivered);
    printf("the application saw every byte, in order, both times: the loss only shows up as latency\n");

    const bool ok = clean.delivered == 8000 && lossy.delivered == 8000 && lossy.rounds > clean.rounds;
    return ok ? 0 : 1;
}
