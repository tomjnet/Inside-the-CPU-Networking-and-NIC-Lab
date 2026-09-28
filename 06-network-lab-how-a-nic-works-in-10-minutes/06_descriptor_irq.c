/* How a NIC Works: RX, TX, DMA and Interrupts - slide 6: descriptor update and the msi-x interrupt (C version of 06_descriptor_irq.cpp) */
/* Build: make 06_descriptor_irq_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* A model of the last two things the card does for a received packet. No device is touched. */
typedef struct {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t status;
} RxDescriptor;

typedef struct { uint16_t len; } Frame;

typedef struct {
    bool masked;        /* set by the driver while it polls */
    bool pending;       /* an interrupt is on its way to the core */
    unsigned fired;     /* how many interrupts this queue raised */
} MsixVector;

enum {
    kDone = 1u << 0,    /* descriptor done */
    kEop  = 1u << 1,    /* end of packet */
    kCsum = 1u << 2     /* checksum verified */
};

static void complete_rx(RxDescriptor *d, Frame f, MsixVector *v) {
    d->length = f.len;                             /* how many bytes landed */
    d->status = (uint16_t)(kDone | kEop | kCsum);  /* one more small DMA write */
    if (v->masked) return;                         /* driver is already polling */
    v->pending = true;                             /* MSI-X: a PCIe write that the */
    ++v->fired;                                    /* APIC turns into a vector, */
}                                                  /* one vector per queue */

static void show(const char *when, const RxDescriptor *d, const MsixVector *v) {
    printf("  %s: length %u  done %d  eop %d  csum ok %d  | interrupts fired %u%s\n", when,
           (unsigned)d->length, (d->status & kDone) ? 1 : 0, (d->status & kEop) ? 1 : 0,
           (d->status & kCsum) ? 1 : 0, v->fired, v->masked ? "  (vector masked)" : "");
}

int main(void) {
    RxDescriptor ring[3] = {{0x10000000ull, 0, 0}, {0x10000800ull, 0, 0}, {0x10001000ull, 0, 0}};
    MsixVector vec = {false, false, 0};

    printf("packet 1 arrives while the driver sleeps:\n");
    show("before", &ring[0], &vec);
    complete_rx(&ring[0], (Frame){1500}, &vec);
    show("after ", &ring[0], &vec);

    /* what the hard IRQ handler does first (slide 7): mask the vector and start polling */
    vec.masked = true;
    vec.pending = false;

    printf("packets 2 and 3 arrive while the driver polls:\n");
    complete_rx(&ring[1], (Frame){64}, &vec);
    show("after ", &ring[1], &vec);
    complete_rx(&ring[2], (Frame){590}, &vec);
    show("after ", &ring[2], &vec);

    printf("3 packets, %u interrupt: the descriptors carry the news, the vector only wakes the driver\n", vec.fired);

    /* correctness: every descriptor is done, and the masked vector stayed silent */
    bool all_done = (ring[0].status & kDone) && (ring[1].status & kDone) && (ring[2].status & kDone);
    return (all_done && vec.fired == 1 && !vec.pending) ? 0 : 1;
}
