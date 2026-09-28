// Kernel Bypass: DPDK, AF_XDP and Low Latency Networking - slide 8: a model of the user space ring
// Build: make 08_rx_ring
#include <algorithm>
#include <cstdint>
#include <iostream>

struct Packet { std::uint32_t len, seq; unsigned char data[56]; };
struct RxRing {                          // 1024 slots of 64 bytes
    Packet slot[1024];
    std::uint32_t head = 0, tail = 0;    // NIC moves head, app tail
    // zero copy: pointers into the ring, no system call
    std::uint32_t rx_burst(Packet** out, std::uint32_t max) {
        std::uint32_t n = std::min(head - tail, max);
        for (std::uint32_t i = 0; i < n; ++i)
            out[i] = &slot[(tail + i) & 1023];   // power of two mask
        tail += n; return n;             // 0 means: poll again
    }
};

static_assert(sizeof(Packet) == 64, "one packet of the model is one cache line");

// The model's NIC: "DMA" writes n packets at head, never past the free space, and numbers them.
static std::uint32_t g_next_seq = 0;
static std::uint32_t nic_dma(RxRing& r, std::uint32_t n) {
    std::uint32_t free_slots = 1024u - (r.head - r.tail);
    n = std::min(n, free_slots);
    for (std::uint32_t i = 0; i < n; ++i) {
        Packet& p = r.slot[(r.head + i) & 1023];
        p.seq = g_next_seq++;
        p.len = 64u + (p.seq & 3u) * 100u;       // 64, 164, 264 or 364 bytes on the wire
        p.data[0] = static_cast<unsigned char>(p.seq & 0xffu);
    }
    r.head += n;
    return n;
}

static RxRing ring;                              // 64 KiB: static storage, not the stack

int main() {
    std::cout << "sizeof(Packet) = " << sizeof(Packet) << " bytes, ring = " << sizeof(ring.slot) / 1024
              << " KiB in " << sizeof(ring.slot) / sizeof(Packet) << " slots\n\n";

    // 100 packets arrive; the application asks for up to 32 at a time
    nic_dma(ring, 100);
    Packet* burst[32];
    std::uint32_t expected = 0;
    bool ok = true;
    for (int poll = 1; poll <= 5; ++poll) {
        std::uint32_t n = ring.rx_burst(burst, 32);
        std::cout << "poll " << poll << ": rx_burst returned " << n;
        if (n > 0) std::cout << "  (seq " << burst[0]->seq << " to " << burst[n - 1]->seq << ")";
        else std::cout << "  (empty: poll again, no sleep)";
        std::cout << "\n";
        for (std::uint32_t i = 0; i < n; ++i) ok = ok && burst[i]->seq == expected++;
    }

    // zero copy: the pointers point into the ring itself
    nic_dma(ring, 1);
    ring.rx_burst(burst, 32);
    std::cout << "\nzero copy: burst[0] = " << static_cast<const void*>(burst[0]) << ", &ring.slot[100] = "
              << static_cast<const void*>(&ring.slot[100]) << "\n";
    ok = ok && burst[0] == &ring.slot[100] && burst[0]->seq == expected++;

    // a full ring: the NIC has nowhere to write, which on real hardware is a drop (rx_missed)
    std::uint32_t accepted = nic_dma(ring, 2000);
    std::cout << "NIC offers 2000 packets to an idle application: " << accepted << " fit, " << 2000 - accepted
              << " would be dropped\n";

    // wrap around: head and tail only grow, the mask finds the slot
    std::uint64_t received = 0;
    for (int round = 0; round < 6; ++round) {
        for (std::uint32_t n = ring.rx_burst(burst, 32); n > 0; n = ring.rx_burst(burst, 32)) {
            for (std::uint32_t i = 0; i < n; ++i) ok = ok && burst[i]->seq == expected++;
            received += n;
        }
        nic_dma(ring, 700);
    }
    std::cout << "after the wrap: head = " << ring.head << ", tail = " << ring.tail << ", slot index of tail = "
              << (ring.tail & 1023) << ", received in order = " << received << "\n";
    std::cout << (ok ? "every packet arrived once and in order\n" : "ERROR: a packet was lost or reordered\n");
    return ok ? 0 : 1;
}
