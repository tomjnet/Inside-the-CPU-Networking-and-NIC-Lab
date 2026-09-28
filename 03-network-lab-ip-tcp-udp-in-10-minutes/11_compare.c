/* IP, TCP and UDP: What Actually Happens to a Packet - slide 11: header sizes and guarantees (C version of 11_compare.cpp) */
/* Build: make 11_compare_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct { const char *name; int header; const char *gives; } Proto;

int main(void) {
    /* header bytes on the wire for one 100 byte message */
    const Proto table[] = {
        {"Ethernet", 14 + 4, "one hop, MAC to MAC, FCS"},
        {"IPv4",     20,     "host to host, best effort, TTL"},
        {"UDP",      8,      "ports, datagrams, no guarantees"},
        {"TCP",      20,     "ports, ordered reliable byte stream"},
    };
    int udp_total = 18 + 20 + 8 + 100;    /* 146 bytes, 46 of overhead */
    int tcp_total = 18 + 20 + 20 + 100;   /* 158 bytes, 58 of overhead */

    printf("%-10s %7s   %s\n", "layer", "header", "what it gives you");
    for (size_t i = 0; i < sizeof table / sizeof table[0]; ++i)
        printf("%-10s %5d B   %s\n", table[i].name, table[i].header, table[i].gives);

    printf("\n100 byte message: %d bytes on the wire with UDP, %d with TCP\n", udp_total, tcp_total);

    const int eth = table[0].header, ip = table[1].header, udp = table[2].header, tcp = table[3].header;
    printf("\n%-9s %12s %12s %14s %14s\n", "message", "UDP on wire", "TCP on wire", "UDP overhead", "TCP overhead");
    const int sizes[] = {10, 100, 1000, 1460};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
        const int n = sizes[i];
        const int u = eth + ip + udp + n;
        const int t = eth + ip + tcp + n;
        printf("%5d B   %10d B %10d B %13.1f%% %13.1f%%\n", n, u, t, 100.0 * (u - n) / u, 100.0 * (t - n) / t);
    }
    printf("(the Ethernet payload has a 46 byte minimum: the 10 byte UDP message is padded on a real link)\n");

    printf("\nwhat each transport makes you wait for:\n");
    printf("  UDP: nothing. No handshake, no ACK, no window: loss and order are the application's job\n");
    printf("  TCP: the handshake (1 round trip), ACKs, the two windows, retransmissions, in order delivery\n");
    printf("  TCP options (timestamps, SACK) often add 12 bytes or more: the header is 20 to 60 bytes\n");

    const bool ok = udp_total == eth + ip + udp + 100 && tcp_total == eth + ip + tcp + 100;
    return ok ? 0 : 1;
}
