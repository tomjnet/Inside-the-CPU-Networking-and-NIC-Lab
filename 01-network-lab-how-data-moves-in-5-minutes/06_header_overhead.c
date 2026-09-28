/* Computer Networking in 5 Minutes: How Data Moves Between Machines - slide 6: headers as structs: the overhead of 100 bytes (C version of 06_header_overhead.cpp) */
/* Build: make 06_header_overhead_c */
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

typedef struct { uint8_t dst[6], src[6]; uint16_t type; } Ethernet;
typedef struct { uint16_t src_port, dst_port, length, checksum; } Udp;
/* Ipv4 (20 B) and Tcp (20 B) are built the same way: see the sample */
typedef struct {
    uint8_t version_ihl, tos;
    uint16_t total_length, id, flags_fragment;
    uint8_t ttl, protocol;
    uint16_t checksum;
    uint32_t src, dst;
} Ipv4;
typedef struct {
    uint16_t src_port, dst_port;
    uint32_t seq, ack;
    uint8_t data_offset, flags;
    uint16_t window, checksum, urgent;
} Tcp;

/* Every field sits on its natural alignment, so no compiler adds padding: the struct is the wire layout. */
_Static_assert(sizeof(Ethernet) == 14, "Ethernet header is 14 bytes");
_Static_assert(sizeof(Ipv4) == 20, "IPv4 header without options is 20 bytes");
_Static_assert(sizeof(Udp) == 8, "UDP header is 8 bytes");
_Static_assert(sizeof(Tcp) == 20, "TCP header without options is 20 bytes");

/* C has no std::vector: a fixed buffer plus its used length, big enough for one frame of this sample. */
enum { FRAME_CAPACITY = 256 };
typedef struct { uint8_t bytes[FRAME_CAPACITY]; size_t size; } Frame;

/* Appends the bytes of one header (or of the data) and returns the offset where it starts. */
static size_t append(Frame *frame, const void *bytes, size_t n) {
    const size_t at = frame->size;
    if (n > FRAME_CAPACITY - at) return (size_t)-1;   /* would overflow: the caller's size check fails */
    memcpy(frame->bytes + at, bytes, n);
    frame->size += n;
    return at;
}

/* Bytes of one Ethernet frame for a message of n bytes. Ethernet pads short payloads up to 46 bytes. */
static size_t frame_bytes(size_t transport_header, size_t n) {
    const size_t min_eth_payload = 46;
    const size_t needed = sizeof(Ipv4) + transport_header + n;
    const size_t eth_payload = needed > min_eth_payload ? needed : min_eth_payload;
    return sizeof(Ethernet) + eth_payload + 4;
}

enum { PAYLOAD = 100, FCS = 4 };   /* the application message; Ethernet trailer, a CRC */

int main(void) {
    const size_t payload = PAYLOAD;
    const size_t fcs = FCS;
    size_t udp_frame = sizeof(Ethernet) + sizeof(Ipv4) + sizeof(Udp)
                     + payload + fcs;    /* 14 + 20 + 8 + 100 + 4 */
    size_t tcp_frame = udp_frame - sizeof(Udp) + sizeof(Tcp);
    printf("UDP: %zu B on the wire, overhead %zu B\n", udp_frame, udp_frame - payload);   /* 146 B, 46 B extra */
    printf("TCP: %zu B on the wire, overhead %zu B\n", tcp_frame, tcp_frame - payload);   /* 158 B, 58 B extra */

    /* Encapsulation for real: data wrapped by UDP, IP and Ethernet, outermost header first on the wire.
       Multi byte fields are left in host byte order here; network byte order is the next episode. */
    Ethernet eth = {{0x02, 0, 0, 0, 0, 0x0B}, {0x02, 0, 0, 0, 0, 0x0A}, 0x0800};
    Ipv4 ip = {0x45, 0, (uint16_t)(sizeof(Ipv4) + sizeof(Udp) + PAYLOAD), 1, 0, 64, 17, 0,
               0x0A000105u, 0x0A000207u};
    Udp udp = {50000, 443, (uint16_t)(sizeof(Udp) + PAYLOAD), 0};
    uint8_t data[PAYLOAD];
    memset(data, 'x', sizeof data);
    const uint8_t crc[FCS] = {0, 0, 0, 0};    /* the NIC computes the real FCS in hardware */

    Frame frame;
    frame.size = 0;
    const size_t at_eth = append(&frame, &eth, sizeof eth);
    const size_t at_ip = append(&frame, &ip, sizeof ip);
    const size_t at_udp = append(&frame, &udp, sizeof udp);
    const size_t at_data = append(&frame, data, sizeof data);
    const size_t at_fcs = append(&frame, crc, sizeof crc);

    printf("\nthe frame, first byte to last\n");
    printf("  offset %3zu: Ethernet header %zu B (MAC addresses)\n", at_eth, sizeof eth);
    printf("  offset %3zu: IPv4 header     %zu B (IP addresses)\n", at_ip, sizeof ip);
    printf("  offset %3zu: UDP header      %zu B (ports)\n", at_udp, sizeof udp);
    printf("  offset %3zu: data            %zu B\n", at_data, sizeof data);
    printf("  offset %3zu: FCS             %zu B\n", at_fcs, sizeof crc);
    printf("  total %zu B\n", frame.size);
    if (frame.size != udp_frame) {
        printf("FAIL: the built frame and the arithmetic disagree\n");
        return 1;
    }

    /* Small messages are mostly headers (and below 18 B of UDP data Ethernet pads the frame to 64 B). */
    printf("\nmessage B   UDP frame B  data %%   TCP frame B  data %%\n");
    const size_t sizes[] = {1, 10, 100, 1000, 1460};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
        const size_t n = sizes[i];
        const size_t u = frame_bytes(sizeof(Udp), n);
        const size_t t = frame_bytes(sizeof(Tcp), n);
        printf("%9zu%14zu%8.1f%14zu%8.1f\n", n, u, 100.0 * (double)n / (double)u,
               t, 100.0 * (double)n / (double)t);
    }
    printf("with an MTU of 1500 one frame carries at most 1472 B over UDP and 1460 B over TCP\n");
    return 0;
}
