// Polling vs Interrupts: Why Low Latency Systems Poll - slide 11: pin and isolate the polling core
// Build: make 11_pin_core
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <sched.h>
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

static std::int64_t percentile(std::vector<std::int64_t>& sorted, double p) {
    return sorted[static_cast<std::size_t>(p * double(sorted.size() - 1))];
}

// One run of the spinning consumer; pin = true makes the consumer thread pin itself first (Linux only).
static void run(const char* label, bool pin) {
    Slot slot;
    std::atomic<std::uint64_t> acked{0};
    std::vector<std::int64_t> lat;
    lat.reserve(kEvents);

    std::thread prod(producer, std::ref(slot), std::cref(acked), nullptr, nullptr);
    std::thread consumer([&] {
#if defined(__linux__)
        if (pin) {
            cpu_set_t allowed;
            CPU_ZERO(&allowed);
            int core = 0;                     // the last core this process may use: the least crowded guess
            if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0)
                for (int c = 0; c < CPU_SETSIZE; ++c)
                    if (CPU_ISSET(c, &allowed)) core = c;

            // keep the polling thread on one core: no migration, warm cache
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(core, &set);                 // ideally an isolated core
            int rc = sched_setaffinity(0, sizeof(set), &set);   // 0: this thread
            if (rc != 0) std::perror("sched_setaffinity");
            // boot with isolcpus=3 nohz_full=3 so nothing else runs there
            // steer the NIC interrupts away: /proc/irq/N/smp_affinity
            std::printf("polling on core %d\n", sched_getcpu());
        }
#else
        (void)pin;
#endif
        std::uint64_t seen = 0;
        for (int i = 0; i < kEvents; ++i) {
            while (slot.seq.load(std::memory_order_acquire) == seen) {}
            ++seen;
            lat.push_back(now_ns() - slot.sent_ns.load());
            acked.fetch_add(1, std::memory_order_release);
        }
    });
    prod.join();
    consumer.join();

    std::sort(lat.begin(), lat.end());
    std::cout << label << ": p50 " << percentile(lat, 0.50) << " ns, p99 " << percentile(lat, 0.99)
              << " ns, max " << lat.back() << " ns (this machine)\n";
}

int main() {
    run("spinning, not pinned", false);
#if defined(__linux__)
    run("spinning, pinned    ", true);
    std::cout << "On an idle machine the two lines look alike. The difference is the tail under load, and it\n"
                 "only becomes stable on real hardware with the core isolated (isolcpus, nohz_full) and the\n"
                 "IRQs moved away. WSL and virtual machines accept the call but share the physical cores.\n";
#else
    std::cout << "this sample needs Linux: it would pin the polling thread with sched_setaffinity, print the\n"
                 "core from sched_getcpu and run the same measurement again on that core.\n";
#endif
    return 0;
}
