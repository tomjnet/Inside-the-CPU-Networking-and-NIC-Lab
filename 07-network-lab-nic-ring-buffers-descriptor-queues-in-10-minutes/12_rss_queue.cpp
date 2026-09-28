// NIC Ring Buffers and Descriptor Queues Explained - slide 12: rss: many rings, one per core
// Build: make 12_rss_queue
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

struct Flow {
    std::uint32_t src_ip, dst_ip;
    std::uint16_t src_port, dst_port;
};

// a simple integer mix, NOT the Toeplitz hash real NICs compute with a secret key;
// what matters for the slide is only that the same flow always gives the same value
static std::uint32_t mix(std::uint32_t h) {
    h ^= h >> 16; h *= 0x7feb352du;
    h ^= h >> 15; h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
static std::uint32_t hash32(std::uint32_t src_ip, std::uint32_t dst_ip, std::uint16_t src_port, std::uint16_t dst_port) {
    std::uint32_t ports = (static_cast<std::uint32_t>(src_port) << 16) | dst_port;
    return mix(mix(mix(src_ip) ^ dst_ip) ^ ports);
}

// RSS: the NIC hashes the flow, the hash picks the ring
static std::uint32_t rss_queue(const Flow& f, std::uint32_t queues) {
    std::uint32_t h = hash32(f.src_ip, f.dst_ip,
                             f.src_port, f.dst_port);
    return h % queues;    // real NICs: a 128 entry lookup table
}
// same flow, same queue, same core: packets stay in order
// 8 queues on 8 cores: no lock between them, warm caches

int main() {
    const std::uint32_t queues = 8;
    std::mt19937 rng(7);                            // fixed seed: the same flows on every run and toolchain

    // 1000 client flows to one server address and port
    std::vector<Flow> flows;
    for (int i = 0; i < 1000; ++i) {
        Flow f{0x0A000000u + static_cast<std::uint32_t>(rng() & 0xFFFFu), 0x0A0000FEu,
               static_cast<std::uint16_t>(1024u + (rng() % 60000u)), 443};
        flows.push_back(f);
    }

    // 100 000 packets, each from a random flow: count per queue, and check the flow never changes queue
    std::vector<std::uint64_t> per_queue(queues, 0);
    std::vector<std::uint32_t> first_queue(flows.size(), queues);
    bool stable = true;
    for (int p = 0; p < 100000; ++p) {
        std::size_t i = rng() % flows.size();
        std::uint32_t q = rss_queue(flows[i], queues);
        if (first_queue[i] == queues) first_queue[i] = q;
        stable = stable && first_queue[i] == q;
        ++per_queue[q];
    }

    std::cout << "1000 flows, 100000 packets, " << queues << " RX queues (one ring, one MSI-X vector, one core each)\n";
    for (std::uint32_t q = 0; q < queues; ++q)
        std::cout << "  queue " << q << " -> core " << q << ": " << per_queue[q] << " packets\n";
    std::cout << "every flow stayed on one queue: " << (stable ? "yes" : "NO")
              << " (so its packets are handled in order by one core)\n";

    // the weak spot: RSS balances flows, not packets. One heavy flow cannot be spread.
    Flow feed{0x0A000001u, 0x0A0000FEu, 30001, 443};
    std::cout << "one market data feed is one flow: all of it lands on queue " << rss_queue(feed, queues)
              << ", whatever the number of queues\n";
    std::cout << "on Linux: ethtool -x eth0 prints the real lookup table, ethtool -l eth0 the number of queues\n";
    return stable ? 0 : 1;
}
