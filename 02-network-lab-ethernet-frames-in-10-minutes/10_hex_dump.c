/* Ethernet Frames in 10 Minutes: MAC Addresses, Headers and Payloads - slide 10: hex dump: read the wire (C version of 10_hex_dump.cpp) */
/* Build: make 10_hex_dump_c */
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

typedef struct {                                                /* slide 9 */
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
} EtherHeader;
_Static_assert(sizeof(EtherHeader) == 14, "the header is 14 bytes");

/* Hex dump: bytes in address order, which is wire order */
static void hex_dump(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; ++i)
        printf("%02x%s", (unsigned)b[i], (i + 1) % 8 == 0 ? "\n" : " ");
    printf("\n");
}

/* The FCS: CRC-32 (polynomial 0x04C11DB7, reflected form 0xEDB88320), bit by bit so the algorithm is visible.
   A NIC does this in hardware at line rate; software never pays for it. */
static uint32_t crc32_update(uint32_t crc, const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

int main(void) {
    EtherHeader h = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
                     {0x02, 0x00, 0x00, 0xaa, 0xbb, 0x01},
                     0};
    h.ethertype = to_big_endian_16(0x0806);

    printf("the 14 byte header:\n");
    hex_dump(&h, sizeof h);
    /* ff ff ff ff ff ff 02 00
       00 aa bb 01 08 06        <- 08 06 on the wire, never 06 08 */

    /* A complete frame: header + ARP request (28 bytes) + padding up to 60 + FCS = 64 bytes, the minimum */
    static unsigned char frame[64];
    memset(frame, 0, sizeof frame);
    memcpy(frame, &h, sizeof h);
    const unsigned char arp[28] = {
        0x00, 0x01,                                     /* hardware type 1: Ethernet */
        0x08, 0x00,                                     /* protocol type 0x0800: IPv4 */
        0x06, 0x04,                                     /* MAC length 6, IP length 4 */
        0x00, 0x01,                                     /* operation 1: request */
        0x02, 0x00, 0x00, 0xaa, 0xbb, 0x01,             /* sender MAC */
        192, 168, 1, 10,                                /* sender IP */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             /* target MAC: unknown, that is the question */
        192, 168, 1, 20};                               /* target IP: who has 192.168.1.20? */
    memcpy(frame + 14, arp, sizeof arp);                /* bytes 42 to 59 stay zero: the padding */
    const uint32_t fcs = ~crc32_update(0xffffffffu, frame, 60);
    for (int i = 0; i < 4; ++i)                         /* the FCS leaves low byte first: the one exception */
        frame[60 + i] = (unsigned char)((fcs >> (8 * i)) & 0xffu);

    printf("the complete 64 byte frame (14 header + 28 ARP + 18 padding + 4 FCS):\n");
    hex_dump(frame, sizeof frame);
    printf("tcpdump shows this frame as length 42: header + ARP. Padding and FCS are work of the NIC.\n");
    printf("FCS = 0x%08x\n", (unsigned)fcs);

    /* what the receiving NIC checks: the CRC over frame plus FCS always leaves the same residue */
    const uint32_t residue = crc32_update(0xffffffffu, frame, 64);
    frame[20] ^= 0x01;                                  /* one flipped bit on the wire */
    const uint32_t damaged = crc32_update(0xffffffffu, frame, 64);
    printf("residue of a good frame   : 0x%08x (always 0xdebb20e3)\n", (unsigned)residue);
    printf("residue with one bit flip : 0x%08x, so the NIC drops the frame silently\n", (unsigned)damaged);

    const unsigned char check[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};   /* "123456789" */
    const uint32_t known = ~crc32_update(0xffffffffu, check, sizeof check);
    const bool ok = known == 0xCBF43926u && residue == 0xDEBB20E3u && damaged != residue
                    && frame[12] == 0x08 && frame[13] == 0x06;
    printf("CRC-32 of the text 123456789 is 0x%08x (reference 0xcbf43926): %s\n",
           (unsigned)known, ok ? "checks passed" : "CHECK FAILED");
    return ok ? 0 : 1;
}
