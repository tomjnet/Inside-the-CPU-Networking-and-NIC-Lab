// Inside the CPU: What We Learned About Networking and NICs - slide 3: three rules: copies, wake ups, local
// Build: make 03_three_rules
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

// the harness of the Low Latency C++ Lab: warm up, repeat, keep minimum and median
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

// rule 2 as a model: typical orders of magnitude, not measurements (episodes 5 and 8 measure them)
struct Stage { const char* name; int copies; int wakeups; double typical_ns; };

static unsigned char app_buffer[1500];                        // the application's buffer that recv fills

int main() {
    unsigned char* volatile escape = app_buffer;              // the compiler cannot prove who else reads it
    unsigned char* buf = escape;

    struct Packet { std::uint16_t len; unsigned char data[1500]; };
    std::vector<Packet> ring(1024);        // power of two: mask, no modulo
    alignas(64) std::size_t head = 0, tail = 0;   // rule 3: own cache lines

    std::mt19937 rng(42);                                     // fixed seed: every run and toolchain does the same work
    std::uint64_t expected = 0;                               // what one pass over the ring must add up to
    for (auto& p : ring) {
        p.len = 1500;
        for (auto& b : p.data) b = static_cast<unsigned char>(rng() & 0xFFu);
        expected += p.data[9];                                // byte 9 of an IPv4 header is the protocol field
    }

    auto copied = bench_ns([&] {           // rule 1: recv copies 24 lines
        for (auto& p : ring) {
            std::memcpy(buf, p.data, p.len); sink(buf[9]);
        }
    });
    auto in_place = bench_ns([&] {         // zero copy: read it in the ring
        for (head += 1024; tail != head; ++tail)   // the NIC moved head
            sink(ring[tail & 1023].data[9]);
    });                                    // rule 2: it polls, no wake up

    // correctness, outside the timing: both readers must see the same bytes, and the ring must be drained
    std::uint64_t sum_copy = 0, sum_ring = 0;
    for (auto& p : ring) { std::memcpy(buf, p.data, p.len); sum_copy += buf[9]; }
    for (head += 1024; tail != head; ++tail) sum_ring += ring[tail & 1023].data[9];
    const bool ok = sum_copy == expected && sum_ring == expected && tail == head;

    const double n = static_cast<double>(ring.size());
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "rule 1, count the copies (" << ring.size() << " packets of 1500 bytes, "
              << (1500 + 63) / 64 << " cache lines each)\n";
    std::cout << "  copy then parse : min " << copied.min_ns / n << " ns, median " << copied.median_ns / n
              << " ns per packet\n";
    std::cout << "  parse in place  : min " << in_place.min_ns / n << " ns, median " << in_place.median_ns / n
              << " ns per packet\n";
    if (in_place.median_ns > 0.0)
        std::cout << "  ratio on this machine: " << copied.median_ns / in_place.median_ns
                  << " times (yours will differ, run it on real Linux hardware)\n";
    std::cout << "  same bytes seen by both readers: " << (ok ? "yes" : "NO") << "\n\n";

    const Stage kernel_path[] = {
        {"NIC, DMA into kernel memory", 0, 0, 1000.0},
        {"interrupt and softirq", 0, 0, 3000.0},
        {"IP and TCP processing", 0, 0, 2000.0},
        {"wake up the blocked thread", 0, 1, 5000.0},
        {"recv: system call plus copy", 1, 0, 500.0},
    };
    const Stage bypass_path[] = {
        {"NIC, DMA into the user ring", 0, 0, 1000.0},
        {"polling core sees tail != head", 0, 0, 50.0},
    };
    std::cout << "rule 2, count the wake ups (a model with typical orders of magnitude, not a measurement)\n";
    auto print_path = [](const char* title, const Stage* stages, std::size_t count) {
        int copies = 0, wakeups = 0;
        double total = 0.0;
        std::cout << "  " << title << "\n";
        for (std::size_t i = 0; i < count; ++i) {
            std::cout << "    " << std::left << std::setw(32) << stages[i].name << std::right << std::setw(8)
                      << stages[i].typical_ns << " ns\n";
            copies += stages[i].copies;
            wakeups += stages[i].wakeups;
            total += stages[i].typical_ns;
        }
        std::cout << "    total " << total << " ns, CPU copies " << copies << ", wake ups " << wakeups << "\n";
    };
    print_path("kernel path", kernel_path, sizeof(kernel_path) / sizeof(kernel_path[0]));
    print_path("bypass path", bypass_path, sizeof(bypass_path) / sizeof(bypass_path[0]));

    const auto head_at = reinterpret_cast<std::uintptr_t>(&head);
    const auto tail_at = reinterpret_cast<std::uintptr_t>(&tail);
    const std::uintptr_t apart = head_at > tail_at ? head_at - tail_at : tail_at - head_at;
    std::cout << "\nrule 3, keep the ring and the core local\n";
    std::cout << "  head and tail are " << apart << " bytes apart: "
              << (apart >= 64 ? "each index has its own cache line\n" : "they share a cache line\n");
    std::cout << "  on real hardware also pin the polling core to the NUMA node of the NIC (taskset, numactl)\n";
    return ok && apart >= 64 ? 0 : 1;
}
