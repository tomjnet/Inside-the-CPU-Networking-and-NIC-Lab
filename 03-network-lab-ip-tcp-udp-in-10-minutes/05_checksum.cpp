// IP, TCP and UDP: What Actually Happens to a Packet - slide 5: the internet checksum in c++
// Build: make 05_checksum
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>

// Internet checksum (RFC 1071): one pass, O(n) bytes, no table
std::uint16_t inet_checksum(const std::uint8_t* p, std::size_t n) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i + 1 < n; i += 2)    // 16 bit big endian
        sum += (std::uint32_t{p[i]} << 8) | p[i + 1];
    if (n % 2 != 0) sum += std::uint32_t{p[n - 1]} << 8;  // odd tail
    while ((sum >> 16) != 0)                      // fold the carries
        sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<std::uint16_t>(~sum & 0xFFFF);
}
// verify: the same sum over a header with its checksum in place = 0

static void store_be16(std::uint8_t* p, unsigned v) {
    p[0] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
    p[1] = static_cast<std::uint8_t>(v & 0xFFU);
}

int main() {
    // 192.168.0.1 to 192.168.0.199, UDP, TTL 64; bytes 10 and 11 are the checksum field, zero for now
    std::uint8_t header[20] = {0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
                               0x00, 0x00, 0xc0, 0xa8, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0xc7};
    int failures = 0;

    // sender: compute with the field at zero, store the answer in the field
    const unsigned sent = inet_checksum(header, sizeof header);
    store_be16(header + 10, sent);
    std::printf("sender:   checksum = 0x%04x (expected 0xb861)\n", sent);
    if (sent != 0xb861U) ++failures;

    // receiver: the same function over the whole header must give zero
    const unsigned check = inet_checksum(header, sizeof header);
    std::printf("receiver: checksum over the valid header = 0x%04x (must be 0)\n", check);
    if (check != 0U) ++failures;

    // a router: TTL minus 1 changes the header, so the checksum is recomputed at every hop
    header[8] = 63;
    const unsigned stale = inet_checksum(header, sizeof header);
    std::printf("router:   TTL 64 to 63, the old checksum verifies to 0x%04x (not 0: stale)\n", stale);
    if (stale == 0U) ++failures;
    store_be16(header + 10, 0U);
    const unsigned hop = inet_checksum(header, sizeof header);
    store_be16(header + 10, hop);
    const unsigned hop_check = inet_checksum(header, sizeof header);
    std::printf("router:   new checksum = 0x%04x, verifies to 0x%04x\n", hop, hop_check);
    if (hop_check != 0U) ++failures;

    // one flipped bit is caught
    header[15] = 0x05;                                    // was 0x01
    const unsigned flipped = inet_checksum(header, sizeof header);
    std::printf("bit flip: verifies to 0x%04x (not 0: detected)\n", flipped);
    if (flipped == 0U) ++failures;
    header[15] = 0x01;

    // the weakness: a sum ignores order, so two swapped 16 bit words go unnoticed
    for (std::size_t i = 0; i < 2; ++i) {
        const std::uint8_t t = header[14 + i];            // low half of src: 00 01
        header[14 + i] = header[18 + i];                  // low half of dst: 00 c7
        header[18 + i] = t;
    }
    const unsigned swapped = inet_checksum(header, sizeof header);
    std::printf("src and dst low words swapped: verifies to 0x%04x (0: NOT detected; the Ethernet CRC would catch it)\n", swapped);
    if (swapped != 0U) ++failures;

    // an odd length takes the padded tail branch
    const std::uint8_t odd[3] = {0x01, 0x02, 0x03};
    const unsigned odd_sum = inet_checksum(odd, sizeof odd);
    std::printf("odd tail: checksum of 01 02 03 = 0x%04x (expected 0xfbfd)\n", odd_sum);
    if (odd_sum != 0xfbfdU) ++failures;

    std::cout << (failures == 0 ? "all checks passed\n" : "CHECK FAILED\n");
    return failures == 0 ? 0 : 1;
}
