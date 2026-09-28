/* Ethernet Frames in 10 Minutes: MAC Addresses, Headers and Payloads - slide 6: how a switch learns macs (C version of 06_switch_learning.cpp) */
/* Build: make 06_switch_learning_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum { kPorts = 3, kTableSize = 16 };
static int floods = 0;
static int forwards = 0;
static int filtered = 0;

static void flood(int in_port) {               /* every port except the one the frame came from */
    ++floods;
    printf("    flood   :");
    for (int p = 1; p <= kPorts; ++p)
        if (p != in_port) printf(" port %d", p);
    printf("\n");
}

static void forward(int port) {
    ++forwards;
    printf("    forward : port %d only\n", port);
}

/* A switch: learn the source, look up the destination. C has no std::unordered_map, so this table is a
   small array searched in order; a real switch keeps it in a hash or CAM and answers in O(1). */
typedef struct { uint64_t mac; int port; } Entry;
static Entry mac_table[kTableSize];            /* MAC -> port */
static int mac_count = 0;

static Entry *find_mac(uint64_t mac) {
    for (int i = 0; i < mac_count; ++i)
        if (mac_table[i].mac == mac) return &mac_table[i];
    return NULL;
}

static void learn(uint64_t mac, int port) {
    Entry *e = find_mac(mac);
    if (e != NULL) { e->port = port; return; }
    if (mac_count < kTableSize) {              /* a full table would age out old entries */
        mac_table[mac_count].mac = mac;
        mac_table[mac_count].port = port;
        ++mac_count;
    }
}

static void on_frame(int in_port, uint64_t src, uint64_t dst) {
    learn(src, in_port);                        /* learn: src is here */
    const Entry *it = find_mac(dst);
    if (it == NULL) flood(in_port);             /* unknown: all ports */
    else if (it->port != in_port) forward(it->port);   /* one port */
    else ++filtered;                            /* same port: the switch drops it (not on the slide) */
}

static void send_frame(const char *what, int in_port, uint64_t src, uint64_t dst) {
    printf("  %s (in on port %d)\n", what, in_port);
    on_frame(in_port, src, dst);
    printf("    table   : %u entries\n", (unsigned)mac_count);
}

int main(void) {
    const uint64_t A = 0x02000000aa01ULL;       /* 48 bits in a 64 bit key */
    const uint64_t B = 0x02000000bb02ULL;
    const uint64_t C = 0x02000000cc03ULL;
    const uint64_t BROADCAST = 0xffffffffffffULL;

    printf("three hosts: A on port 1, B on port 2, C on port 3, empty MAC table\n\n");
    send_frame("1. A sends to B: B is unknown", 1, A, B);
    send_frame("2. B replies to A: A was learned in step 1", 2, B, A);
    send_frame("3. A sends to B again: C sees nothing", 1, A, B);
    send_frame("4. C sends an ARP request to broadcast: never a source, so never in the table", 3, C, BROADCAST);
    send_frame("5. B answers C with a unicast frame", 2, B, C);

    printf("\nfloods %d, forwards %d, filtered %d\n", floods, forwards, filtered);
    const bool ok = floods == 2 && forwards == 3 && filtered == 0 && mac_count == 3;
    printf("expected 2 floods (one unknown, one broadcast) and 3 forwards: %s\n",
           ok ? "checks passed" : "CHECK FAILED");
    return ok ? 0 : 1;
}
