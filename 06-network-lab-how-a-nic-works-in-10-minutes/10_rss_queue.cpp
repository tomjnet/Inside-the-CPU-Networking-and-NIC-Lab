// How a NIC Works: RX, TX, DMA and Interrupts - slide 10: offloads: checksum, tso and rss
// Build: make 10_rss_queue
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

// A model of receive side scaling plus the arithmetic of TSO. The hash is a toy: real cards use the
// Toeplitz hash with a secret key and an indirection table, but the property shown here is the same.
struct Flow {
    std::uint32_t src_ip;
    std::uint32_t dst_ip;
    std::uint16_t src_port;
    std::uint16_t dst_port;
};

// RSS: the NIC hashes the flow and picks the RX queue (the core)
std::uint32_t rss_queue(const Flow& f, std::uint32_t queues) {
    std::uint32_t h = f.src_ip ^ (f.dst_ip * 2654435761u);
    h ^= (std::uint32_t{f.src_port} << 16) | f.dst_port;
    h *= 2246822519u;              // real NICs: the Toeplitz hash
    return (h >> 16) % queues;     // plus an indirection table
}   // same flow, same queue, same core: in order, warm cache

int main() {
    const std::uint32_t queues = 4;
    const std::uint32_t server = 0x0a000001u;   // 10.0.0.1

    std::cout << "six flows to 10.0.0.1:443, five packets each, " << queues << " RX queues:\n";
    bool stable = true;
    for (std::uint16_t i = 0; i < 6; ++i) {
        Flow f = {0x0a000064u + i, server, static_cast<std::uint16_t>(40000 + 7 * i), 443};
        std::uint32_t first = rss_queue(f, queues);
        for (int packet = 1; packet < 5; ++packet) stable = stable && (rss_queue(f, queues) == first);
        std::cout << "  flow from 10.0.0." << (100 + i) << ":" << f.src_port << "  -> queue " << first
                  << " (core " << first << ") for every packet\n";
    }

    std::mt19937 rng(42);   // fixed seed: two runs and two toolchains do the same work
    std::vector<long> per_queue(queues, 0);
    const int flows = 100000;
    for (int i = 0; i < flows; ++i) {
        Flow f = {static_cast<std::uint32_t>(rng()), server, static_cast<std::uint16_t>(rng() & 0xffffu), 443};
        ++per_queue[rss_queue(f, queues)];
    }
    std::cout << "\n" << flows << " random flows spread over the queues:\n";
    long low = flows, high = 0;
    for (std::size_t q = 0; q < per_queue.size(); ++q) {
        std::cout << "  queue " << q << ": " << per_queue[q] << "\n";
        if (per_queue[q] < low) low = per_queue[q];
        if (per_queue[q] > high) high = per_queue[q];
    }
    std::cout << "many flows balance well; one heavy flow still lands on one queue and one core\n\n";

    // TSO: how many wire frames the card makes from one segment the stack hands over
    const int segment = 64 * 1024;
    const int mss = 1448;   // 1500 MTU minus IP (20), TCP (20) and timestamps (12)
    const int frames = (segment + mss - 1) / mss;
    std::cout << "TSO: one " << segment << " byte segment becomes " << frames << " frames of up to " << mss
              << " payload bytes; the stack ran once instead of " << frames << " times\n";
    std::cout << "checksum offload: the result arrives as a status bit of the RX descriptor (see 06_descriptor_irq)\n";

    // correctness: a flow never changes queue, and no queue is starved
    return (stable && low > flows / 8 && high < flows / 2 && frames == 46) ? 0 : 1;
}
