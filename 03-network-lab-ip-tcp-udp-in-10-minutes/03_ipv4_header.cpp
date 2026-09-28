// IP, TCP and UDP: What Actually Happens to a Packet - slide 3: the ipv4 header: 20 bytes
// Build: make 03_ipv4_header
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>

// IPv4 header: 20 bytes without options, every field big endian
struct Ipv4Header {
    std::uint8_t  version_ihl;   // 0x45: version 4, 5 words of 32 bits
    std::uint8_t  tos;           // DSCP and ECN
    std::uint16_t total_length;  // header plus payload, in bytes
    std::uint16_t id;            // fragmentation: only named here
    std::uint16_t flags_offset;  // 0x4000 = do not fragment
    std::uint8_t  ttl;           // minus 1 at every router, 0 = dropped
    std::uint8_t  protocol;      // 6 = TCP, 17 = UDP, 1 = ICMP
    std::uint16_t checksum;      // covers the header only
    std::uint32_t src, dst;      // the two IPv4 addresses
};                               // sizeof == 20, no padding needed

static_assert(sizeof(Ipv4Header) == 20, "the fields are naturally aligned: no padding");
static_assert(offsetof(Ipv4Header, ttl) == 8, "ttl is byte 8 on the wire");
static_assert(offsetof(Ipv4Header, src) == 12, "src is byte 12 on the wire");

// portable byte order helper (episode 2): the wire is big endian, x86 is little endian
static unsigned read_be16(const std::uint8_t* p) {
    return (static_cast<unsigned>(p[0]) << 8) | static_cast<unsigned>(p[1]);
}

static void print_address(const char* label, const std::uint8_t* p) {
    std::printf("  %-13s %u.%u.%u.%u\n", label, static_cast<unsigned>(p[0]), static_cast<unsigned>(p[1]),
                static_cast<unsigned>(p[2]), static_cast<unsigned>(p[3]));
}

static const char* protocol_name(unsigned protocol) {
    switch (protocol) {
        case 1: return "ICMP";
        case 6: return "TCP";
        case 17: return "UDP";
        default: return "other";
    }
}

int main() {
    // a header as it travels: 192.168.0.1 sends 115 bytes of UDP to 192.168.0.199
    const std::uint8_t wire[20] = {0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
                                   0xb8, 0x61, 0xc0, 0xa8, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0xc7};

    Ipv4Header h{};
    std::memcpy(&h, wire, sizeof h);      // same layout as the wire; multi byte fields are still big endian

    std::cout << "sizeof(Ipv4Header) = " << sizeof(Ipv4Header) << " bytes\n";
    std::cout << "fields of the sample header:\n";
    const unsigned ihl = static_cast<unsigned>(h.version_ihl) & 0x0FU;
    std::printf("  %-13s %u\n", "version", static_cast<unsigned>(h.version_ihl) >> 4);
    std::printf("  %-13s %u words = %u bytes\n", "header length", ihl, ihl * 4U);
    std::printf("  %-13s %u\n", "tos", static_cast<unsigned>(h.tos));
    std::printf("  %-13s %u bytes (header plus payload)\n", "total length",
                read_be16(wire + offsetof(Ipv4Header, total_length)));
    std::printf("  %-13s %u\n", "id", read_be16(wire + offsetof(Ipv4Header, id)));
    const unsigned flags = read_be16(wire + offsetof(Ipv4Header, flags_offset));
    std::printf("  %-13s 0x%04x%s\n", "flags, offset", flags, (flags & 0x4000U) != 0 ? " (do not fragment)" : "");
    std::printf("  %-13s %u hops left\n", "ttl", static_cast<unsigned>(h.ttl));
    std::printf("  %-13s %u (%s)\n", "protocol", static_cast<unsigned>(h.protocol), protocol_name(h.protocol));
    std::printf("  %-13s 0x%04x\n", "checksum", read_be16(wire + offsetof(Ipv4Header, checksum)));
    print_address("src", wire + offsetof(Ipv4Header, src));
    print_address("dst", wire + offsetof(Ipv4Header, dst));

    // why the helper matters: a struct member read directly is in host byte order
    std::printf("total_length read without a swap: %u (the right value is 115; x86 is little endian)\n",
                static_cast<unsigned>(h.total_length));
    std::printf("id 0x%04x, flags_offset 0x%04x, checksum 0x%04x, src 0x%08x, dst 0x%08x: host order views\n",
                static_cast<unsigned>(h.id), static_cast<unsigned>(h.flags_offset), static_cast<unsigned>(h.checksum),
                static_cast<unsigned>(h.src), static_cast<unsigned>(h.dst));
    std::cout << "what IP does not have: ports, sequence numbers, ACKs. Best effort delivery only.\n";
    return 0;
}
