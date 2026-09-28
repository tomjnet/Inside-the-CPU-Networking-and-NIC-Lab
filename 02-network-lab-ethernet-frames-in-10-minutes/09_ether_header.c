/* Ethernet Frames in 10 Minutes: MAC Addresses, Headers and Payloads - slide 9: the header as a c++ struct (C version of 09_ether_header.cpp) */
/* Build: make 09_ether_header_c */
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

/* slide 8, with a run time host check: C has no std::endian */
static bool host_is_little(void) {
    const uint16_t one = 1;
    unsigned char first;
    memcpy(&first, &one, 1);
    return first == 1;
}
static uint16_t to_big_endian_16(uint16_t x) {
    if (!host_is_little())
        return x;
    return (uint16_t)((x << 8) | (x >> 8));
}

/* The 14 byte header, laid out exactly as it sits on the wire. The same struct is plain C. */
typedef struct {
    uint8_t  dst[6];        /* offset 0: destination MAC first */
    uint8_t  src[6];        /* offset 6: source MAC */
    uint16_t ethertype;     /* offset 12: big endian on the wire */
} EtherHeader;
_Static_assert(sizeof(EtherHeader) == 14, "no padding: 12 % 2 == 0");

/* A header that does NOT line up: one byte, then 32 bits. The compiler pads it, so it cannot mirror the wire
   without #pragma pack. The portable answer is the byte writer below. */
typedef struct {
    uint8_t  kind;
    uint32_t value;
} Misaligned;

/* the portable way: write every byte yourself, no layout and no byte order question left */
static void write_header(unsigned char *out, const uint8_t *dst, const uint8_t *src, uint16_t ethertype) {
    memcpy(out, dst, 6);
    memcpy(out + 6, src, 6);
    out[12] = (unsigned char)(ethertype >> 8);
    out[13] = (unsigned char)(ethertype & 0xff);
}

int main(void) {
    EtherHeader h = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff},   /* broadcast */
                     {0x02, 0x00, 0x00, 0xaa, 0xbb, 0x01},   /* a local MAC */
                     0};
    h.ethertype = to_big_endian_16(0x0806);                  /* ARP */

    printf("sizeof(EtherHeader) = %u, offsets: dst %u, src %u, ethertype %u\n",
           (unsigned)sizeof(EtherHeader), (unsigned)offsetof(EtherHeader, dst),
           (unsigned)offsetof(EtherHeader, src), (unsigned)offsetof(EtherHeader, ethertype));
    printf("sizeof(Misaligned)  = %u, not 5: the compiler padded it, the wire would not\n\n",
           (unsigned)sizeof(Misaligned));

    /* receive side: convert back to host order before comparing, or compare with a converted constant */
    printf("ethertype in host order: 0x%04x (%s)\n", (unsigned)to_big_endian_16(h.ethertype),
           h.ethertype == to_big_endian_16(0x0806) ? "ARP" : "something else");

    unsigned char wire[14];
    write_header(wire, h.dst, h.src, 0x0806);
    const bool same = memcmp(wire, &h, sizeof h) == 0;
    printf("struct bytes equal to the byte by byte writer: %s\n", same ? "yes" : "NO");

    /* what a driver does with a buffer the NIC filled: overlay the header on the first 14 bytes */
    EtherHeader seen;
    memcpy(&seen, wire, sizeof seen);           /* memcpy, not a cast: no aliasing or alignment question */
    const bool ok = same && to_big_endian_16(seen.ethertype) == 0x0806 && seen.dst[0] == 0xff
                    && seen.src[5] == 0x01;
    printf("header read back from the wire bytes: %s\n", ok ? "checks passed" : "CHECK FAILED");
    return ok ? 0 : 1;
}
