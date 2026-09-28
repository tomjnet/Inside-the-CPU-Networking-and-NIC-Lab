// IP, TCP and UDP: What Actually Happens to a Packet - slide 6: udp: 8 bytes and no state
// Build: make 06_udp_header
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>

// UDP header: 8 bytes, no connection, no per peer state in the kernel
struct UdpHeader {
    std::uint16_t src_port;   // who sent it, big endian
    std::uint16_t dst_port;   // which socket receives it
    std::uint16_t length;     // header plus data, in bytes
    std::uint16_t checksum;   // optional in IPv4, 0 = not computed
};                            // sizeof == 8
// one sendto() = one datagram: delivered whole or not at all
// no handshake, no ACK, no retransmission, no ordering
// multicast: one datagram to a 239.x.y.z group, every subscriber

static_assert(sizeof(UdpHeader) == 8, "four fields of 16 bits, no padding");

static void store_be16(std::uint8_t* p, unsigned v) {
    p[0] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
    p[1] = static_cast<std::uint8_t>(v & 0xFFU);
}

// what a subscriber of a multicast feed does: the feed numbers its own messages, UDP does not
struct FeedState {
    unsigned next_seq = 1;
    unsigned gaps = 0;
    unsigned processed = 0;
};

static void on_datagram(FeedState& s, unsigned seq) {
    if (seq < s.next_seq) {
        std::printf("  seq %u: old or duplicate, ignored\n", seq);
        return;
    }
    if (seq > s.next_seq) {
        std::printf("  seq %u: GAP, %u message(s) missing: the application decides (snapshot, second feed)\n", seq,
                    seq - s.next_seq);
        s.gaps += seq - s.next_seq;
    } else {
        std::printf("  seq %u: in order\n", seq);
    }
    s.next_seq = seq + 1;
    ++s.processed;
}

int main() {
    // the header of a 100 byte message from port 40000 to port 30001, in host order first
    const UdpHeader h{40000, 30001, static_cast<std::uint16_t>(sizeof(UdpHeader) + 100), 0};

    std::uint8_t wire[sizeof(UdpHeader)] = {};
    store_be16(wire + 0, h.src_port);
    store_be16(wire + 2, h.dst_port);
    store_be16(wire + 4, h.length);
    store_be16(wire + 6, h.checksum);

    std::cout << "sizeof(UdpHeader) = " << sizeof(UdpHeader) << " bytes\n";
    std::printf("src_port %u, dst_port %u, length %u (8 of header plus 100 of data), checksum %u (not computed)\n",
                static_cast<unsigned>(h.src_port), static_cast<unsigned>(h.dst_port), static_cast<unsigned>(h.length),
                static_cast<unsigned>(h.checksum));
    std::cout << "on the wire, big endian:";
    for (std::size_t i = 0; i < sizeof wire; ++i) std::printf(" %02x", static_cast<unsigned>(wire[i]));
    std::cout << "\n";
    std::cout << "largest UDP payload in one 1500 byte MTU: " << 1500 - 20 - 8 << " bytes (no IP fragmentation)\n";

    // UDP may lose, duplicate or reorder: a feed carries its own sequence numbers and the subscriber watches them
    std::cout << "a subscriber of a multicast feed, datagrams as they arrive:\n";
    const unsigned arrivals[] = {1, 2, 3, 5, 6, 6, 4, 7};
    FeedState state;
    for (unsigned seq : arrivals) on_datagram(state, seq);
    std::printf("processed %u, missing %u, and nobody waited: the newest price was never held back\n", state.processed,
                state.gaps);
    return (state.processed == 6 && state.gaps == 1) ? 0 : 1;
}
