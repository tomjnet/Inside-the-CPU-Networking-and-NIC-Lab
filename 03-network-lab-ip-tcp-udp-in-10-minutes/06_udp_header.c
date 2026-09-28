/* IP, TCP and UDP: What Actually Happens to a Packet - slide 6: udp: 8 bytes and no state (C version of 06_udp_header.cpp) */
/* Build: make 06_udp_header_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* UDP header: 8 bytes, no connection, no per peer state in the kernel */
typedef struct {
    uint16_t src_port;   /* who sent it, big endian */
    uint16_t dst_port;   /* which socket receives it */
    uint16_t length;     /* header plus data, in bytes */
    uint16_t checksum;   /* optional in IPv4, 0 = not computed */
} UdpHeader;             /* sizeof == 8 */
/* one sendto() = one datagram: delivered whole or not at all */
/* no handshake, no ACK, no retransmission, no ordering */
/* multicast: one datagram to a 239.x.y.z group, every subscriber */

_Static_assert(sizeof(UdpHeader) == 8, "four fields of 16 bits, no padding");

static void store_be16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFU);
    p[1] = (uint8_t)(v & 0xFFU);
}

/* what a subscriber of a multicast feed does: the feed numbers its own messages, UDP does not */
typedef struct {
    unsigned next_seq;
    unsigned gaps;
    unsigned processed;
} FeedState;

/* C has no references: the state travels as a pointer */
static void on_datagram(FeedState *s, unsigned seq) {
    if (seq < s->next_seq) {
        printf("  seq %u: old or duplicate, ignored\n", seq);
        return;
    }
    if (seq > s->next_seq) {
        printf("  seq %u: GAP, %u message(s) missing: the application decides (snapshot, second feed)\n", seq,
               seq - s->next_seq);
        s->gaps += seq - s->next_seq;
    } else {
        printf("  seq %u: in order\n", seq);
    }
    s->next_seq = seq + 1;
    ++s->processed;
}

int main(void) {
    /* the header of a 100 byte message from port 40000 to port 30001, in host order first */
    const UdpHeader h = {40000, 30001, (uint16_t)(sizeof(UdpHeader) + 100), 0};

    uint8_t wire[sizeof(UdpHeader)] = {0};
    store_be16(wire + 0, h.src_port);
    store_be16(wire + 2, h.dst_port);
    store_be16(wire + 4, h.length);
    store_be16(wire + 6, h.checksum);

    printf("sizeof(UdpHeader) = %u bytes\n", (unsigned)sizeof(UdpHeader));
    printf("src_port %u, dst_port %u, length %u (8 of header plus 100 of data), checksum %u (not computed)\n",
           (unsigned)h.src_port, (unsigned)h.dst_port, (unsigned)h.length, (unsigned)h.checksum);
    printf("on the wire, big endian:");
    for (size_t i = 0; i < sizeof wire; ++i) printf(" %02x", (unsigned)wire[i]);
    printf("\n");
    printf("largest UDP payload in one 1500 byte MTU: %d bytes (no IP fragmentation)\n", 1500 - 20 - 8);

    /* UDP may lose, duplicate or reorder: a feed carries its own sequence numbers and the subscriber watches them */
    printf("a subscriber of a multicast feed, datagrams as they arrive:\n");
    const unsigned arrivals[] = {1, 2, 3, 5, 6, 6, 4, 7};
    FeedState state = {1, 0, 0};
    for (size_t i = 0; i < sizeof arrivals / sizeof arrivals[0]; ++i) on_datagram(&state, arrivals[i]);
    printf("processed %u, missing %u, and nobody waited: the newest price was never held back\n", state.processed,
           state.gaps);
    return (state.processed == 6 && state.gaps == 1) ? 0 : 1;
}
