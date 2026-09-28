// Polling vs Interrupts: Why Low Latency Systems Poll - slide 10: p50, p99 and cpu time
// Build: make 10_measure
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <time.h>
#endif

static std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct alignas(64) Slot {
    std::atomic<std::uint64_t> seq{0};      // bumped on every publish
    std::atomic<std::int64_t> sent_ns{0};   // when the producer wrote
};

static void publish(Slot& s) {              // two stores, no lock
    s.sent_ns.store(now_ns(), std::memory_order_relaxed);
    s.seq.fetch_add(1, std::memory_order_release);
}

constexpr int kEvents = 2000;                 // messages per run
constexpr std::int64_t kGapNs = 100000;       // the wire is quiet for about 100 us between messages

// The producer plays the NIC. It paces itself with a busy wait (a sleep would be far too coarse), publishes,
// and then waits until the consumer has taken the message, so no message is ever overwritten.
static void producer(Slot& slot, const std::atomic<std::uint64_t>& acked, std::mutex* m,
                     std::condition_variable* cv) {
    for (int i = 0; i < kEvents; ++i) {
        const std::int64_t next = now_ns() + kGapNs;
        while (now_ns() < next) {}
        if (m != nullptr) {                   // blocking consumer: publish under the mutex, then wake it
            {
                std::lock_guard<std::mutex> lock(*m);
                publish(slot);
            }
            cv->notify_one();
        } else {
            publish(slot);                    // spinning consumer: nothing else to do
        }
        while (acked.load(std::memory_order_acquire) != static_cast<std::uint64_t>(i) + 1) std::this_thread::yield();
    }
}

#if defined(__linux__)
static double thread_cpu_ms() {               // CPU time this thread really used, not wall time
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return double(ts.tv_sec) * 1e3 + double(ts.tv_nsec) / 1e6;
}
#else
static double thread_cpu_ms() { return -1.0; }   // needs Linux: CLOCK_THREAD_CPUTIME_ID
#endif

struct Result { const char* name; std::int64_t p50; std::int64_t p99; double cpu_ms; double wall_ms; };
static Result g_results[2];
static int g_count = 0;

static void report(const char* name, std::int64_t p50, std::int64_t p99, double cpu_ms) {
    g_results[g_count++] = Result{name, p50, p99, cpu_ms, 0.0};
}

int main() {
    for (int style = 0; style < 2; ++style) {
        const bool blocking = style == 0;
        const char* name = blocking ? "blocking" : "spinning";
        Slot slot;
        std::mutex m;
        std::condition_variable cv;
        std::atomic<std::uint64_t> acked{0};
        std::vector<std::int64_t> lat;
        lat.reserve(kEvents);

        const std::int64_t t0 = now_ns();
        std::thread prod(producer, std::ref(slot), std::cref(acked), blocking ? &m : nullptr, blocking ? &cv : nullptr);
        std::thread consumer([&] {
            std::uint64_t seen = 0;
            for (int i = 0; i < kEvents; ++i) {
                if (blocking) {
                    std::unique_lock<std::mutex> lock(m);
                    cv.wait(lock, [&] { return slot.seq.load() != seen; });
                } else {
                    while (slot.seq.load(std::memory_order_acquire) == seen) {}
                }
                ++seen;
                lat.push_back(now_ns() - slot.sent_ns.load());
                acked.fetch_add(1, std::memory_order_release);
            }

            // sort the samples once, then read the percentiles
            std::sort(lat.begin(), lat.end());
            auto pct = [&](double p) {
                return lat[static_cast<std::size_t>(p * double(lat.size() - 1))];
            };
            report(name, pct(0.50), pct(0.99), thread_cpu_ms());
            // typical: blocking p50 tens of us, spinning p50 under 1 us
            // CPU time: blocking near zero, spinning equals the wall time
        });
        prod.join();
        consumer.join();
        g_results[style].wall_ms = double(now_ns() - t0) / 1e6;
    }

    std::cout << "consumer   p50 ns     p99 ns     CPU ms     wall ms   (this machine)\n";
    for (const Result& r : g_results) {
        std::cout << r.name << "   " << r.p50 << "        " << r.p99 << "        ";
        if (r.cpu_ms < 0.0) std::cout << "n/a";
        else std::cout << r.cpu_ms;
        std::cout << "        " << r.wall_ms << "\n";
    }
    if (g_results[1].p50 > 0)
        std::cout << "p50 ratio, blocking over spinning: " << double(g_results[0].p50) / double(g_results[1].p50)
                  << "x on this machine\n";
    if (g_results[0].cpu_ms < 0.0)
        std::cout << "CPU time of one thread needs Linux (CLOCK_THREAD_CPUTIME_ID): there the blocking consumer\n"
                     "shows almost none and the spinning consumer shows the whole wall time.\n";
    else
        std::cout << "Read the CPU column: blocking used almost none, spinning used the whole wall time.\n";
    std::cout << "A virtual machine or a busy desktop stretches the tail: take numbers on quiet Linux hardware.\n";
    return g_count == 2 ? 0 : 1;
}
