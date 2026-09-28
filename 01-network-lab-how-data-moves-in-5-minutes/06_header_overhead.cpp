// Computer Networking in 5 Minutes: How Data Moves Between Machines - slide 6: headers as structs: the overhead of 100 bytes
// Build: make 06_header_overhead
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

struct Ethernet { std::uint8_t dst[6], src[6]; std::uint16_t type; };
struct Udp { std::uint16_t src_port, dst_port, length, checksum; };
// Ipv4 (20 B) and Tcp (20 B) are built the same way: see the sample
struct Ipv4 {
    std::uint8_t version_ihl, tos;
    std::uint16_t total_length, id, flags_fragment;
    std::uint8_t ttl, protocol;
    std::uint16_t checksum;
    std::uint32_t src, dst;
};
struct Tcp {
    std::uint16_t src_port, dst_port;
    std::uint32_t seq, ack;
    std::uint8_t data_offset, flags;
    std::uint16_t window, checksum, urgent;
};

// Every field sits on its natural alignment, so no compiler adds padding: the struct is the wire layout.
static_assert(sizeof(Ethernet) == 14, "Ethernet header is 14 bytes");
static_assert(sizeof(Ipv4) == 20, "IPv4 header without options is 20 bytes");
static_assert(sizeof(Udp) == 8, "UDP header is 8 bytes");
static_assert(sizeof(Tcp) == 20, "TCP header without options is 20 bytes");

// Appends the bytes of one header (or of the data) and returns the offset where it starts.
static std::size_t append(std::vector<std::uint8_t>& frame, const void* bytes, std::size_t n) {
    const std::size_t at = frame.size();
    const auto* p = static_cast<const std::uint8_t*>(bytes);
    frame.insert(frame.end(), p, p + n);
    return at;
}

// Bytes of one Ethernet frame for a message of n bytes. Ethernet pads short payloads up to 46 bytes.
static std::size_t frame_bytes(std::size_t transport_header, std::size_t n) {
    const std::size_t min_eth_payload = 46;
    const std::size_t eth_payload = std::max(min_eth_payload, sizeof(Ipv4) + transport_header + n);
    return sizeof(Ethernet) + eth_payload + 4;
}

int main() {
    constexpr std::size_t payload = 100;      // the application message
    constexpr std::size_t fcs = 4;            // Ethernet trailer, a CRC
    std::size_t udp_frame = sizeof(Ethernet) + sizeof(Ipv4) + sizeof(Udp)
                          + payload + fcs;    // 14 + 20 + 8 + 100 + 4
    std::size_t tcp_frame = udp_frame - sizeof(Udp) + sizeof(Tcp);
    std::cout << "UDP: " << udp_frame << " B on the wire, overhead "
              << udp_frame - payload << " B\n";    // 146 B, 46 B extra
    std::cout << "TCP: " << tcp_frame << " B on the wire, overhead "
              << tcp_frame - payload << " B\n";    // 158 B, 58 B extra

    // Encapsulation for real: data wrapped by UDP, IP and Ethernet, outermost header first on the wire.
    // Multi byte fields are left in host byte order here; network byte order is the next episode.
    Ethernet eth{{0x02, 0, 0, 0, 0, 0x0B}, {0x02, 0, 0, 0, 0, 0x0A}, 0x0800};
    Ipv4 ip{0x45, 0, static_cast<std::uint16_t>(sizeof(Ipv4) + sizeof(Udp) + payload), 1, 0, 64, 17, 0,
            0x0A000105, 0x0A000207};
    Udp udp{50000, 443, static_cast<std::uint16_t>(sizeof(Udp) + payload), 0};
    const std::vector<std::uint8_t> data(payload, std::uint8_t{'x'});
    const std::uint8_t crc[fcs] = {0, 0, 0, 0};    // the NIC computes the real FCS in hardware

    std::vector<std::uint8_t> frame;
    const std::size_t at_eth = append(frame, &eth, sizeof eth);
    const std::size_t at_ip = append(frame, &ip, sizeof ip);
    const std::size_t at_udp = append(frame, &udp, sizeof udp);
    const std::size_t at_data = append(frame, data.data(), data.size());
    const std::size_t at_fcs = append(frame, crc, sizeof crc);

    std::cout << "\nthe frame, first byte to last\n";
    std::cout << "  offset " << std::setw(3) << at_eth << ": Ethernet header " << sizeof eth << " B (MAC addresses)\n";
    std::cout << "  offset " << std::setw(3) << at_ip << ": IPv4 header     " << sizeof ip << " B (IP addresses)\n";
    std::cout << "  offset " << std::setw(3) << at_udp << ": UDP header      " << sizeof udp << " B (ports)\n";
    std::cout << "  offset " << std::setw(3) << at_data << ": data            " << data.size() << " B\n";
    std::cout << "  offset " << std::setw(3) << at_fcs << ": FCS             " << sizeof crc << " B\n";
    std::cout << "  total " << frame.size() << " B\n";
    if (frame.size() != udp_frame) {
        std::cout << "FAIL: the built frame and the arithmetic disagree\n";
        return 1;
    }

    // Small messages are mostly headers (and below 18 B of UDP data Ethernet pads the frame to 64 B).
    std::cout << "\nmessage B   UDP frame B  data %   TCP frame B  data %\n" << std::fixed << std::setprecision(1);
    const std::size_t sizes[] = {1, 10, 100, 1000, 1460};
    for (std::size_t n : sizes) {
        const std::size_t u = frame_bytes(sizeof(Udp), n);
        const std::size_t t = frame_bytes(sizeof(Tcp), n);
        std::cout << std::setw(9) << n << std::setw(14) << u << std::setw(8)
                  << 100.0 * static_cast<double>(n) / static_cast<double>(u) << std::setw(14) << t << std::setw(8)
                  << 100.0 * static_cast<double>(n) / static_cast<double>(t) << "\n";
    }
    std::cout << "with an MTU of 1500 one frame carries at most 1472 B over UDP and 1460 B over TCP\n";
    return 0;
}
