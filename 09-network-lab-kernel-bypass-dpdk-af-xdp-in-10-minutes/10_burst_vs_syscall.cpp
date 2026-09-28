// Kernel Bypass: DPDK, AF_XDP and Low Latency Networking - slide 10: a burst against a system call per packet
// Build: make 10_burst_vs_syscall
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
// A real kernel crossing, the cheapest there is: getppid does no work inside the kernel.
static void kernel_crossing() { (void)syscall(SYS_getppid); }
static const bool kRealCrossing = true;
#else
static void kernel_crossing() {}
static const bool kRealCrossing = false;
#endif

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

// kernel path model: one crossing and one copy for every packet
std::uint32_t recv_model(RxRing& r, Packet& user_buf) {
    kernel_crossing();                   // typical 100 to 300 ns
    if (r.head == r.tail) return 0;
    user_buf = r.slot[r.tail++ & 1023];  // the copy to user space
    return user_buf.len;
}

constexpr std::uint32_t kPackets = 65536;        // per timed run, a multiple of 256 and of 4

// The kernel way: ask for one packet at a time, each answer is a crossing plus a copy.
static std::uint64_t drain_with_recv(RxRing& r) {
    Packet user_buf{};
    std::uint64_t bytes = 0;
    for (std::uint32_t got = 0; got < kPackets;) {
        std::uint32_t len = recv_model(r, user_buf);
        if (len == 0) { nic_dma(r, 256); continue; }          // the model's NIC delivers when the ring is dry
        bytes += len + user_buf.data[0];
        ++got;
    }
    return bytes;
}

// The bypass way: up to 32 pointers per poll, packets handled where the NIC wrote them.
static std::uint64_t drain_with_bursts(RxRing& r) {
    Packet* burst[32];
    std::uint64_t bytes = 0;
    for (std::uint32_t got = 0; got < kPackets;) {
        std::uint32_t n = r.rx_burst(burst, 32);
        if (n == 0) { nic_dma(r, 256); continue; }
        for (std::uint32_t i = 0; i < n; ++i) bytes += burst[i]->len + burst[i]->data[0];
        got += n;
    }
    return bytes;
}

static RxRing ring;                              // 64 KiB: static storage, not the stack

int main() {
    if (!kRealCrossing)
        std::cout << "this sample needs Linux: the kernel crossing is a real system call there, here it costs "
                     "nothing,\nso add about 100 to 300 ns per packet (typical) to the first line\n\n";

    // correctness first: both ways must see the same bytes
    std::uint64_t a = drain_with_recv(ring);
    std::uint64_t b = drain_with_bursts(ring);

    auto per_packet = bench_ns([&] { sink(drain_with_recv(ring)); });
    auto per_burst = bench_ns([&] { sink(drain_with_bursts(ring)); });

    const double n = static_cast<double>(kPackets);
    std::cout << "this machine, " << kPackets << " packets per run, ns per packet (NIC model included)\n";
    std::cout << "  recv model, 1 crossing + 1 copy each: min " << per_packet.min_ns / n << "  median "
              << per_packet.median_ns / n << "\n";
    std::cout << "  rx_burst of 32, zero copy:            min " << per_burst.min_ns / n << "  median "
              << per_burst.median_ns / n << "\n";
    std::cout << "  ratio of the medians: " << per_packet.median_ns / per_burst.median_ns << "x\n\n";
    std::cout << "crossings per run: " << kPackets + kPackets / 256 << " against 0; copies: " << kPackets
              << " against 0\n";
    std::cout << "the model is kind to the kernel: no interrupt, no protocol code, no wake up of a sleeping thread\n";

    bool ok = a == b;
    std::cout << (ok ? "both paths saw the same bytes\n" : "ERROR: the two paths disagree\n");
    return ok ? 0 : 1;
}
