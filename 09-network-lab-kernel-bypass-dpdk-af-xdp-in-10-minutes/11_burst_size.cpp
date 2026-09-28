// Kernel Bypass: DPDK, AF_XDP and Low Latency Networking - slide 11: burst size: throughput against latency
// Build: make 11_burst_size
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

static volatile std::uint64_t g_sink = 0;
static void sink(std::uint64_t x) { g_sink = g_sink + x; }   // keeps the result alive: the loop cannot be deleted

struct BenchResult { double min_ns; double median_ns; };

template <class F>
static BenchResult bench_ns(F&& f, int warmup = 3, int repeats = 21) {
    for (int i = 0; i < warmup; ++i) f();                     // warm up: caches, branch predictor, page faults
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        f();
        auto t1 = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    return BenchResult{samples.front(), samples[samples.size() / 2]};
}

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

constexpr std::uint32_t kPackets = 262144;       // per timed run, a multiple of 1024

// Drain kPackets from the ring with a given burst size; the NIC refills a full ring when it runs dry.
static std::uint64_t drain(RxRing& r, std::uint32_t max_burst) {
    Packet* burst[64];
    std::uint64_t bytes = 0;
    for (std::uint32_t got = 0; got < kPackets;) {
        std::uint32_t n = r.rx_burst(burst, max_burst);
        if (n == 0) { nic_dma(r, 1024); continue; }
        for (std::uint32_t i = 0; i < n; ++i) bytes += burst[i]->len;
        got += n;
    }
    return bytes;
}

static RxRing ring;                              // 64 KiB: static storage, not the stack

int main() {
    std::printf("the model: per packet = work + poll / burst, the last packet waits (burst - 1) * work\n");
    std::printf("burst  per packet   last waits\n");
    // typical costs, not measurements: replace them with your own
    constexpr double kPollNs = 40.0;     // fixed cost of one rx_burst
    constexpr double kWorkNs = 50.0;     // handling one packet
    for (int burst : {1, 4, 8, 16, 32, 64}) {
        double per_packet = kWorkNs + kPollNs / burst;  // amortized poll
        double last_waits = (burst - 1) * kWorkNs;      // queue in burst
        std::printf("%5d %8.1f ns %8.1f ns\n",
                    burst, per_packet, last_waits);
    }
    std::printf("\nthroughput wants a big burst, the tail wants a small one; 32 is the usual default\n\n");

    // the same sweep on the ring model: here the work is one add, so the poll cost is what you see
    std::printf("this machine, ring model, %u packets per run\n", static_cast<unsigned>(kPackets));
    std::printf("burst  median ns per packet\n");
    const std::uint64_t expected = static_cast<std::uint64_t>(kPackets / 4) * (64u + 164u + 264u + 364u);
    bool ok = true;
    for (std::uint32_t burst : {1u, 4u, 8u, 16u, 32u, 64u}) {
        ok = ok && drain(ring, burst) == expected;
        BenchResult r = bench_ns([&] { sink(drain(ring, burst)); });
        std::printf("%5u %10.2f\n", static_cast<unsigned>(burst), r.median_ns / static_cast<double>(kPackets));
    }
    std::printf("%s\n", ok ? "every burst size saw the same bytes" : "ERROR: a burst size lost packets");
    return ok ? 0 : 1;
}
