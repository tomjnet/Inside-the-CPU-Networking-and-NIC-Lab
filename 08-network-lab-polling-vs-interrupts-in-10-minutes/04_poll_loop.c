/* Polling vs Interrupts: Why Low Latency Systems Poll - slide 4: a core that spins on the ring (C version of 04_poll_loop.cpp) */
/* Build: make 04_poll_loop_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* A model of the RX ring of episode 7: the NIC moves head, the driver moves tail, indexes are masked. */
#define K_SIZE   8u
#define K_MASK   (K_SIZE - 1u)
#define K_PASSES 1000000u                     /* the demo is bounded: a real poller never stops */
#define K_EVERY  1000u                        /* the model NIC delivers one packet every 1000 passes */

typedef struct { uint64_t id; } Packet;
typedef struct {
    Packet slots[K_SIZE];
    uint64_t head;                            /* written by the NIC */
    uint64_t tail;                            /* written by the driver */
} Ring;

static uint64_t g_sum = 0;
static void handle(const Packet *p) { g_sum += p->id; }

/* The model NIC: now and then it writes a packet at head, as DMA would. Returns false when the demo ends. */
static bool nic_model_step(Ring *ring, uint64_t polls) {
    if (polls >= K_PASSES) return false;
    if (polls % K_EVERY == K_EVERY - 1 && ring->head - ring->tail < K_SIZE) {
        ring->slots[ring->head & K_MASK].id = ring->head + 1;
        ++ring->head;
    }
    return true;
}

int main(void) {
    Ring ring = {0};

    /* one core, one RX ring, no interrupt: the driver keeps asking */
    uint64_t polls = 0, empty = 0, packets = 0;
    while (nic_model_step(&ring, polls)) {   /* the NIC moves head */
        ++polls;
        if (ring.head == ring.tail) {        /* one read of a cached line */
            ++empty;                         /* nothing yet: ask again */
            continue;
        }
        handle(&ring.slots[ring.tail & K_MASK]);   /* seen within one pass */
        ++ring.tail;                         /* no wake up, no switch */
        ++packets;
    }

    printf("passes of the loop : %" PRIu64 "\n", polls);
    printf("empty passes       : %" PRIu64 "\n", empty);
    printf("packets handled    : %" PRIu64 "\n", packets);
    printf("empty per packet   : %" PRIu64 "\n", packets != 0 ? empty / packets : 0);
    printf("Every packet was seen in the first pass after head moved: no interrupt, no wake up.\n");
    printf("The price is the first number: the core never stopped asking.\n");

    const uint64_t expected = K_PASSES / K_EVERY;
    const bool ok = packets == expected && g_sum == expected * (expected + 1) / 2 && ring.head == ring.tail;
    printf(ok ? "check: every packet handled once\n" : "check FAILED: a packet was lost\n");
    return ok ? 0 : 1;
}
