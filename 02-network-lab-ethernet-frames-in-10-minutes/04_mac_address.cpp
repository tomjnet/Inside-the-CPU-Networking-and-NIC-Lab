// Ethernet Frames in 10 Minutes: MAC Addresses, Headers and Payloads - slide 4: mac addresses
// Build: make 04_mac_address
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

// A MAC address: 48 bits, 6 bytes, written aa:bb:cc:dd:ee:ff
using Mac = std::array<std::uint8_t, 6>;

// bit 0 of the FIRST byte: 0 = unicast, 1 = multicast (a group)
bool is_multicast(const Mac& m) { return (m[0] & 0x01) != 0; }
// bit 1 of the first byte: 1 = locally administered (VMs, WSL)
bool is_local(const Mac& m)     { return (m[0] & 0x02) != 0; }
// all ones: every station on the segment takes the frame
bool is_broadcast(const Mac& m) {
    return std::all_of(m.begin(), m.end(),
                       [](std::uint8_t b) { return b == 0xff; });
}

static void describe(const char* what, const Mac& m) {
    std::printf("  %02x:%02x:%02x:%02x:%02x:%02x  %-9s  %-8s  %s\n",
                static_cast<unsigned>(m[0]), static_cast<unsigned>(m[1]), static_cast<unsigned>(m[2]),
                static_cast<unsigned>(m[3]), static_cast<unsigned>(m[4]), static_cast<unsigned>(m[5]),
                is_broadcast(m) ? "broadcast" : (is_multicast(m) ? "multicast" : "unicast"),
                is_local(m) ? "local" : "factory",
                what);
}

int main() {
    std::printf("sizeof(Mac) = %u bytes: a byte array, never a number, never byte swapped\n\n",
                static_cast<unsigned>(sizeof(Mac)));
    std::printf("  address            kind       assigned  example\n");
    describe("a physical NIC (vendor prefix 3c:fd:fe)", Mac{0x3c, 0xfd, 0xfe, 0x12, 0x34, 0x56});
    describe("a Hyper-V or WSL adapter (vendor prefix 00:15:5d)", Mac{0x00, 0x15, 0x5d, 0xa1, 0xb2, 0xc3});
    describe("a locally administered address (bit 1 set)", Mac{0x02, 0x00, 0x00, 0xaa, 0xbb, 0x01});
    describe("IPv4 multicast 224.0.0.1 (bit 0 set)", Mac{0x01, 0x00, 0x5e, 0x00, 0x00, 0x01});
    describe("broadcast: all 48 bits set", Mac{0xff, 0xff, 0xff, 0xff, 0xff, 0xff});

    // the three tests must agree with the definitions
    const Mac bcast{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const Mac card{0x3c, 0xfd, 0xfe, 0x12, 0x34, 0x56};
    const bool ok = is_broadcast(bcast) && is_multicast(bcast) && !is_broadcast(card) && !is_multicast(card)
                    && !is_local(card);
    std::printf("\nbroadcast is the group address with every bit set: %s\n", ok ? "checks passed" : "CHECK FAILED");
    return ok ? 0 : 1;
}
