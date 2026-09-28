/* Ethernet Frames in 10 Minutes: MAC Addresses, Headers and Payloads - slide 8: network byte order (C version of 08_to_big_endian.cpp) */
/* Build: make 08_to_big_endian_c */
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

#if defined(__linux__)
#include <arpa/inet.h>
#endif

/* C has no std::endian and no if constexpr: ask the host once at run time by looking at the first byte of 1.
   The compiler folds this to a constant, so the branch below costs nothing. */
static bool host_is_little(void) {
    const uint16_t one = 1;
    unsigned char first;
    memcpy(&first, &one, 1);
    return first == 1;
}

/* Host to network order for 16 bits: what htons does. No copies */
static uint16_t to_big_endian_16(uint16_t x) {
    if (!host_is_little())
        return x;                               /* already wire order */
    return (uint16_t)((x << 8) | (x >> 8));
}
/* on x86 (little endian): 0x0800 is stored as 00 08, sent as 08 00.
   C has no constexpr function, so the "swap is its own inverse" check runs in main instead of _Static_assert. */

/* the 32 bit sibling: what htonl does (an IPv4 address, a sequence number) */
static uint32_t to_big_endian_32(uint32_t x) {
    if (!host_is_little())
        return x;
    return ((x & 0x000000ffu) << 24) | ((x & 0x0000ff00u) << 8)
         | ((x & 0x00ff0000u) >> 8) | ((x & 0xff000000u) >> 24);
}

/* the fully portable way: no question about the host at all, write the high byte first */
static void put_be16(unsigned char *out, uint16_t x) {
    out[0] = (unsigned char)(x >> 8);
    out[1] = (unsigned char)(x & 0xff);
}

/* no template in C: a pointer plus a size shows the bytes of any object */
static void show_bytes(const char *what, const void *value, size_t n) {
    const unsigned char *b = (const unsigned char *)value;
    printf("  %-34s", what);
    for (size_t i = 0; i < n; ++i) printf(" %02x", (unsigned)b[i]);
    printf("\n");
}

int main(void) {
    const bool little = host_is_little();
    printf("this machine is %s endian\n\n", little ? "little" : "big");

    const uint16_t ethertype = 0x0800;                      /* IPv4 */
    const uint32_t ip = 0xC0A80114u;                        /* 192.168.1.20 */
    const uint16_t net = to_big_endian_16(ethertype);
    const uint32_t ip_net = to_big_endian_32(ip);
    printf("bytes in memory, lowest address first (the order they would leave on the wire):\n");
    show_bytes("uint16_t 0x0800 as stored", &ethertype, sizeof ethertype);
    show_bytes("to_big_endian_16(0x0800)", &net, sizeof net);
    show_bytes("uint32_t 0xC0A80114 as stored", &ip, sizeof ip);
    show_bytes("to_big_endian_32(0xC0A80114)", &ip_net, sizeof ip_net);

    unsigned char wire[2];
    put_be16(wire, ethertype);
    printf("  %-34s %02x %02x\n", "put_be16: byte writes, any host", (unsigned)wire[0], (unsigned)wire[1]);

    /* correctness: the converted value must sit in memory as 08 00, and the swap must be its own inverse */
    unsigned char mem[2];
    memcpy(mem, &net, 2);
    bool ok = mem[0] == 0x08 && mem[1] == 0x00 && wire[0] == 0x08 && wire[1] == 0x00;
    ok = ok && to_big_endian_16(net) == ethertype && to_big_endian_32(ip_net) == ip;

#if defined(__linux__)
    /* the sockets API does the same job: htons = host to network short, htonl = host to network long */
    const bool same = htons(ethertype) == net && htonl(ip) == ip_net && ntohs(net) == ethertype;
    printf("\nhtons(0x0800) == to_big_endian_16(0x0800), and htonl agrees too: %s\n", same ? "yes" : "NO");
    ok = ok && same;
#else
    printf("\nthis sample needs Linux: one more line would compare htons and htonl with our functions\n");
#endif

    printf("wire order is 08 00 and the swap is its own inverse: %s\n", ok ? "checks passed" : "CHECK FAILED");
    return ok ? 0 : 1;
}
