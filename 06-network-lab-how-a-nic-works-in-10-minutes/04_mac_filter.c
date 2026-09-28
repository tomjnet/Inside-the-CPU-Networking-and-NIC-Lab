/* How a NIC Works: RX, TX, DMA and Interrupts - slide 4: phy, mac and the rx fifo (C version of 04_mac_filter.cpp) */
/* Build: make 04_mac_filter_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A model of the first two blocks of the card. Nothing here touches a real NIC. */
typedef struct { uint8_t b[6]; } Mac;   /* C has no std::array ==: wrap the bytes, compare with memcmp */

typedef struct {
    Mac dst;
    uint16_t len;           /* bytes on the wire */
    bool fcs_ok;            /* result of the CRC32 check the MAC does in hardware */
    const char *note;
} Frame;

typedef struct {
    size_t used;
    size_t capacity;
    unsigned missed;        /* what ethtool -S shows as rx_missed or rx_fifo_errors */
} Fifo;

static const Mac kBroadcast = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

static bool mac_equal(const Mac *a, const Mac *b) { return memcmp(a->b, b->b, sizeof a->b) == 0; }

/* MAC: the first filter, in hardware, before any DMA or CPU work */
static bool mac_accepts(const Frame *f, const Mac *mine, bool promisc) {
    if (!f->fcs_ok) return false;              /* bad CRC32: dropped, counted */
    if (promisc) return true;                  /* tcpdump mode: accept all */
    if (mac_equal(&f->dst, mine)) return true; /* unicast for this port */
    return mac_equal(&f->dst, &kBroadcast);    /* ff:ff:ff:ff:ff:ff */
}
/* RX FIFO: a few hundred KiB on the chip absorb a burst */
static bool fifo_push(Fifo *q, const Frame *f) {
    if (q->used + f->len > q->capacity) { ++q->missed; return false; }
    q->used += f->len; return true;            /* full FIFO: rx_missed grows */
}

static unsigned run_burst(Fifo *q, const Frame *f, int frames, int drain_every) {
    unsigned accepted = 0;
    for (int i = 0; i < frames; ++i) {
        if (fifo_push(q, f)) ++accepted;
        /* the DMA engine empties one frame every drain_every arrivals (0 = the host is stuck) */
        if (drain_every > 0 && i % drain_every == 0 && q->used >= f->len) q->used -= f->len;
    }
    return accepted;
}

int main(void) {
    const Mac mine = {{0x02, 0x00, 0x00, 0xaa, 0xbb, 0x01}};
    const Mac other = {{0x02, 0x00, 0x00, 0xaa, 0xbb, 0x02}};
    Frame frames[4];
    frames[0] = (Frame){mine, 1500, true, "unicast for this port"};
    frames[1] = (Frame){kBroadcast, 64, true, "broadcast (an ARP request)"};
    frames[2] = (Frame){other, 1500, true, "unicast for another port"};
    frames[3] = (Frame){mine, 1500, false, "for this port, but the CRC32 is wrong"};

    printf("MAC filter, normal mode and promiscuous mode:\n");
    for (size_t i = 0; i < sizeof frames / sizeof frames[0]; ++i) {
        const Frame *f = &frames[i];
        printf("  %s | %s  %s\n", mac_accepts(f, &mine, false) ? "accept" : "drop  ",
               mac_accepts(f, &mine, true) ? "accept" : "drop  ", f->note);
    }
    printf("  a frame with a bad FCS dies in the MAC in both modes: the CPU never sees it\n\n");

    const Frame full = {mine, 1500, true, "burst"};
    Fifo stuck = {0, 256 * 1024, 0};
    unsigned ok1 = run_burst(&stuck, &full, 400, 0);
    Fifo slow = {0, 256 * 1024, 0};
    unsigned ok2 = run_burst(&slow, &full, 400, 2);
    Fifo fast = {0, 256 * 1024, 0};
    unsigned ok3 = run_burst(&fast, &full, 400, 1);

    printf("RX FIFO of 256 KiB, burst of 400 frames of 1500 bytes:\n");
    printf("  host stuck (no DMA)      accepted %u  rx_missed %u\n", ok1, stuck.missed);
    printf("  DMA drains every 2nd     accepted %u  rx_missed %u\n", ok2, slow.missed);
    printf("  DMA keeps up             accepted %u  rx_missed %u\n", ok3, fast.missed);
    printf("  drops here mean the host side is too slow, not the network\n");

    /* correctness: a FIFO that is drained as fast as it fills must not lose a frame */
    return (fast.missed == 0 && ok3 == 400) ? 0 : 1;
}
