/* IP, TCP and UDP: What Actually Happens to a Packet - slide 3: the ipv4 header: 20 bytes (C version of 03_ipv4_header.cpp) */
/* Build: make 03_ipv4_header_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* IPv4 header: 20 bytes without options, every field big endian */
typedef struct {
    uint8_t  version_ihl;   /* 0x45: version 4, 5 words of 32 bits */
    uint8_t  tos;           /* DSCP and ECN */
    uint16_t total_length;  /* header plus payload, in bytes */
    uint16_t id;            /* fragmentation: only named here */
    uint16_t flags_offset;  /* 0x4000 = do not fragment */
    uint8_t  ttl;           /* minus 1 at every router, 0 = dropped */
    uint8_t  protocol;      /* 6 = TCP, 17 = UDP, 1 = ICMP */
    uint16_t checksum;      /* covers the header only */
    uint32_t src, dst;      /* the two IPv4 addresses */
} Ipv4Header;               /* sizeof == 20, no padding needed */

_Static_assert(sizeof(Ipv4Header) == 20, "the fields are naturally aligned: no padding");
_Static_assert(offsetof(Ipv4Header, ttl) == 8, "ttl is byte 8 on the wire");
_Static_assert(offsetof(Ipv4Header, src) == 12, "src is byte 12 on the wire");

/* portable byte order helper (episode 2): the wire is big endian, x86 is little endian */
static unsigned read_be16(const uint8_t *p) {
    return ((unsigned)p[0] << 8) | (unsigned)p[1];
}

static void print_address(const char *label, const uint8_t *p) {
    printf("  %-13s %u.%u.%u.%u\n", label, (unsigned)p[0], (unsigned)p[1], (unsigned)p[2], (unsigned)p[3]);
}

static const char *protocol_name(unsigned protocol) {
    switch (protocol) {
        case 1: return "ICMP";
        case 6: return "TCP";
        case 17: return "UDP";
        default: return "other";
    }
}

int main(void) {
    /* a header as it travels: 192.168.0.1 sends 115 bytes of UDP to 192.168.0.199 */
    const uint8_t wire[20] = {0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
                              0xb8, 0x61, 0xc0, 0xa8, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0xc7};

    Ipv4Header h;
    memset(&h, 0, sizeof h);
    memcpy(&h, wire, sizeof h);      /* same layout as the wire; multi byte fields are still big endian */

    printf("sizeof(Ipv4Header) = %u bytes\n", (unsigned)sizeof(Ipv4Header));
    printf("fields of the sample header:\n");
    const unsigned ihl = (unsigned)h.version_ihl & 0x0FU;
    printf("  %-13s %u\n", "version", (unsigned)h.version_ihl >> 4);
    printf("  %-13s %u words = %u bytes\n", "header length", ihl, ihl * 4U);
    printf("  %-13s %u\n", "tos", (unsigned)h.tos);
    printf("  %-13s %u bytes (header plus payload)\n", "total length",
           read_be16(wire + offsetof(Ipv4Header, total_length)));
    printf("  %-13s %u\n", "id", read_be16(wire + offsetof(Ipv4Header, id)));
    const unsigned flags = read_be16(wire + offsetof(Ipv4Header, flags_offset));
    printf("  %-13s 0x%04x%s\n", "flags, offset", flags, (flags & 0x4000U) != 0 ? " (do not fragment)" : "");
    printf("  %-13s %u hops left\n", "ttl", (unsigned)h.ttl);
    printf("  %-13s %u (%s)\n", "protocol", (unsigned)h.protocol, protocol_name(h.protocol));
    printf("  %-13s 0x%04x\n", "checksum", read_be16(wire + offsetof(Ipv4Header, checksum)));
    print_address("src", wire + offsetof(Ipv4Header, src));
    print_address("dst", wire + offsetof(Ipv4Header, dst));

    /* why the helper matters: a struct member read directly is in host byte order */
    printf("total_length read without a swap: %u (the right value is 115; x86 is little endian)\n",
           (unsigned)h.total_length);
    printf("id 0x%04x, flags_offset 0x%04x, checksum 0x%04x, src 0x%08x, dst 0x%08x: host order views\n",
           (unsigned)h.id, (unsigned)h.flags_offset, (unsigned)h.checksum, (unsigned)h.src, (unsigned)h.dst);
    printf("what IP does not have: ports, sequence numbers, ACKs. Best effort delivery only.\n");
    return 0;
}
