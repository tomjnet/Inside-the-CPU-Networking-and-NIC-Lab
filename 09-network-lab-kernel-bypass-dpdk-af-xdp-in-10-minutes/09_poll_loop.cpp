// Kernel Bypass: DPDK, AF_XDP and Low Latency Networking - slide 9: the poll mode loop
// Build: make 09_poll_loop
#include <algorithm>
#include <chrono>
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

// The model's NIC: "DMA" writes n packets at head, never past the free space, and numbers them.
// On real hardware this runs in parallel with the loop; here it runs when the ring is dry, so the
// output is the same on every machine.
static std::uint32_t g_next_seq = 0;
static void nic_dma(RxRing& r, std::uint32_t n) {
    std::uint32_t free_slots = 1024u - (r.head - r.tail);
    n = std::min(n, free_slots);
    for (std::uint32_t i = 0; i < n; ++i) {
        Packet& p = r.slot[(r.head + i) & 1023];
        p.seq = g_next_seq++;
        p.len = 64u + (p.seq & 3u) * 100u;       // 64, 164, 264 or 364 bytes on the wire
    }
    r.head += n;
}

// The application's work on one packet, in place: check the order, account the bytes.
static std::uint32_t g_expected_seq = 0;
static std::uint64_t g_out_of_order = 0;
static std::uint64_t handle(const Packet& p) {
    if (p.seq != g_expected_seq) ++g_out_of_order;
    ++g_expected_seq;
    return p.len;
}

static RxRing ring;                              // 64 KiB: static storage, not the stack

int main() {
    const std::uint64_t total = 1000000;
    auto t0 = std::chrono::steady_clock::now();

    // the poll mode loop: one pinned core, never sleeps, never traps
    Packet* burst[32];
    std::uint64_t packets = 0, bytes = 0, empty_polls = 0;
    while (packets < total) {
        std::uint32_t n = ring.rx_burst(burst, 32);   // up to 32 pointers
        if (n == 0) {                  // nothing yet: no sleep, ask again
            ++empty_polls;
            nic_dma(ring, 256);        // model: the NIC delivers 256 more
        }
        for (std::uint32_t i = 0; i < n; ++i) bytes += handle(*burst[i]);
        packets += n;
    }

    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();

    // 1,000,000 packets, a quarter of each length: 64, 164, 264, 364 bytes
    const std::uint64_t expected_bytes = (total / 4) * (64u + 164u + 264u + 364u);
    std::cout << "packets     " << packets << "\n";
    std::cout << "bytes       " << bytes << " (expected " << expected_bytes << ")\n";
    std::cout << "empty polls " << empty_polls << " (one for every 256 packets in this model)\n";
    std::cout << "full polls  " << packets / 32 << " of 32 packets each\n";
    std::cout << "system calls, sleeps and copies in the loop: 0\n";
    std::cout << "this machine: " << ns / static_cast<double>(packets) << " ns per packet, NIC model included\n";
    std::cout << "on a real link the empty polls are the price: the core spins at 100 percent while it waits\n";

    bool ok = packets == total && bytes == expected_bytes && g_out_of_order == 0;
    std::cout << (ok ? "every packet handled once and in order\n" : "ERROR: a packet was lost or reordered\n");
    return ok ? 0 : 1;
}
