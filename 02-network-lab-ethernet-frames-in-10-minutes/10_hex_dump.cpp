// Ethernet Frames in 10 Minutes: MAC Addresses, Headers and Payloads - slide 10: hex dump: read the wire
// Build: make 10_hex_dump
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

struct EtherHeader {                                            // slide 9
    std::uint8_t  dst[6];
    std::uint8_t  src[6];
    std::uint16_t ethertype;
};
static_assert(sizeof(EtherHeader) == 14);

// Hex dump: bytes in address order, which is wire order
void hex_dump(const void* p, std::size_t n) {
    const auto* b = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i)
        std::printf("%02x%s", b[i], (i + 1) % 8 == 0 ? "\n" : " ");
    std::printf("\n");
}

// The FCS: CRC-32 (polynomial 0x04C11DB7, reflected form 0xEDB88320), bit by bit so the algorithm is visible.
// A NIC does this in hardware at line rate; software never pays for it.
static std::uint32_t crc32_update(std::uint32_t crc, const unsigned char* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

int main() {
    EtherHeader h{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
                  {0x02, 0x00, 0x00, 0xaa, 0xbb, 0x01},
                  to_big_endian_16(0x0806)};

    std::printf("the 14 byte header:\n");
    hex_dump(&h, sizeof h);
    // ff ff ff ff ff ff 02 00
    // 00 aa bb 01 08 06        <- 08 06 on the wire, never 06 08

    // A complete frame: header + ARP request (28 bytes) + padding up to 60 + FCS = 64 bytes, the minimum
    static unsigned char frame[64];
    std::memset(frame, 0, sizeof frame);
    std::memcpy(frame, &h, sizeof h);
    const unsigned char arp[28] = {
        0x00, 0x01,                                     // hardware type 1: Ethernet
        0x08, 0x00,                                     // protocol type 0x0800: IPv4
        0x06, 0x04,                                     // MAC length 6, IP length 4
        0x00, 0x01,                                     // operation 1: request
        0x02, 0x00, 0x00, 0xaa, 0xbb, 0x01,             // sender MAC
        192, 168, 1, 10,                                // sender IP
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // target MAC: unknown, that is the question
        192, 168, 1, 20};                               // target IP: who has 192.168.1.20?
    std::memcpy(frame + 14, arp, sizeof arp);           // bytes 42 to 59 stay zero: the padding
    const std::uint32_t fcs = ~crc32_update(0xffffffffu, frame, 60);
    for (int i = 0; i < 4; ++i)                         // the FCS leaves low byte first: the one exception
        frame[60 + i] = static_cast<unsigned char>((fcs >> (8 * i)) & 0xffu);

    std::printf("the complete 64 byte frame (14 header + 28 ARP + 18 padding + 4 FCS):\n");
    hex_dump(frame, sizeof frame);
    std::printf("tcpdump shows this frame as length 42: header + ARP. Padding and FCS are work of the NIC.\n");
    std::printf("FCS = 0x%08x\n", static_cast<unsigned>(fcs));

    // what the receiving NIC checks: the CRC over frame plus FCS always leaves the same residue
    const std::uint32_t residue = crc32_update(0xffffffffu, frame, 64);
    frame[20] ^= 0x01;                                  // one flipped bit on the wire
    const std::uint32_t damaged = crc32_update(0xffffffffu, frame, 64);
    std::printf("residue of a good frame   : 0x%08x (always 0xdebb20e3)\n", static_cast<unsigned>(residue));
    std::printf("residue with one bit flip : 0x%08x, so the NIC drops the frame silently\n",
                static_cast<unsigned>(damaged));

    const unsigned char check[] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};   // "123456789"
    const std::uint32_t known = ~crc32_update(0xffffffffu, check, sizeof check);
    const bool ok = known == 0xCBF43926u && residue == 0xDEBB20E3u && damaged != residue
                    && frame[12] == 0x08 && frame[13] == 0x06;
    std::printf("CRC-32 of the text 123456789 is 0x%08x (reference 0xcbf43926): %s\n",
                static_cast<unsigned>(known), ok ? "checks passed" : "CHECK FAILED");
    return ok ? 0 : 1;
}
