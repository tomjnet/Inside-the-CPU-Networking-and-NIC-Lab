// Ethernet Frames in 10 Minutes: MAC Addresses, Headers and Payloads - slide 9: the header as a c++ struct
// Build: make 09_ether_header
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr std::uint16_t to_big_endian_16(std::uint16_t x) {     // slide 8
    if constexpr (std::endian::native == std::endian::big)
        return x;
    else
        return static_cast<std::uint16_t>((x << 8) | (x >> 8));
}

// The 14 byte header, laid out exactly as it sits on the wire
struct EtherHeader {
    std::uint8_t  dst[6];       // offset 0: destination MAC first
    std::uint8_t  src[6];       // offset 6: source MAC
    std::uint16_t ethertype;    // offset 12: big endian on the wire
};
static_assert(sizeof(EtherHeader) == 14);   // no padding: 12 % 2 == 0

// A header that does NOT line up: one byte, then 32 bits. The compiler pads it, so it cannot mirror the wire
// without #pragma pack. The portable answer is the byte writer below.
struct Misaligned {
    std::uint8_t  kind;
    std::uint32_t value;
};

// the portable way: write every byte yourself, no layout and no byte order question left
static void write_header(unsigned char* out, const std::uint8_t* dst, const std::uint8_t* src,
                         std::uint16_t ethertype) {
    std::memcpy(out, dst, 6);
    std::memcpy(out + 6, src, 6);
    out[12] = static_cast<unsigned char>(ethertype >> 8);
    out[13] = static_cast<unsigned char>(ethertype & 0xff);
}

int main() {
    EtherHeader h{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff},   // broadcast
                  {0x02, 0x00, 0x00, 0xaa, 0xbb, 0x01},   // a local MAC
                  to_big_endian_16(0x0806)};              // ARP

    std::printf("sizeof(EtherHeader) = %u, offsets: dst %u, src %u, ethertype %u\n",
                static_cast<unsigned>(sizeof(EtherHeader)), static_cast<unsigned>(offsetof(EtherHeader, dst)),
                static_cast<unsigned>(offsetof(EtherHeader, src)),
                static_cast<unsigned>(offsetof(EtherHeader, ethertype)));
    std::printf("sizeof(Misaligned)  = %u, not 5: the compiler padded it, the wire would not\n\n",
                static_cast<unsigned>(sizeof(Misaligned)));

    // receive side: convert back to host order before comparing, or compare with a converted constant
    std::printf("ethertype in host order: 0x%04x (%s)\n", static_cast<unsigned>(to_big_endian_16(h.ethertype)),
                h.ethertype == to_big_endian_16(0x0806) ? "ARP" : "something else");

    unsigned char wire[14];
    write_header(wire, h.dst, h.src, 0x0806);
    const bool same = std::memcmp(wire, &h, sizeof h) == 0;
    std::printf("struct bytes equal to the byte by byte writer: %s\n", same ? "yes" : "NO");

    // what a driver does with a buffer the NIC filled: overlay the header on the first 14 bytes
    EtherHeader seen;
    std::memcpy(&seen, wire, sizeof seen);      // memcpy, not a cast: no aliasing or alignment question
    const bool ok = same && to_big_endian_16(seen.ethertype) == 0x0806 && seen.dst[0] == 0xff
                    && seen.src[5] == 0x01;
    std::printf("header read back from the wire bytes: %s\n", ok ? "checks passed" : "CHECK FAILED");
    return ok ? 0 : 1;
}
