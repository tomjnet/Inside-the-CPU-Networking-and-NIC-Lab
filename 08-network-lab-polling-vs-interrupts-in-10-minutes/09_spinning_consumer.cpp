// Polling vs Interrupts: Why Low Latency Systems Poll - slide 9: the consumer that spins
// Build: make 09_spinning_consumer
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

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

int main() {
    Slot slot;
    std::atomic<std::uint64_t> acked{0};
    std::vector<std::int64_t> lat;
    lat.reserve(kEvents);
    auto record = [&](std::int64_t ns) {      // keep the sample, then tell the producer it may go on
        lat.push_back(ns);
        acked.fetch_add(1, std::memory_order_release);
    };

    std::thread prod(producer, std::ref(slot), std::cref(acked), nullptr, nullptr);
    std::thread consumer([&] {
        // the polling style: never sleep, ask the slot again and again
        std::uint64_t seen = 0, empty_polls = 0;
        for (int i = 0; i < kEvents; ++i) {
            while (slot.seq.load(std::memory_order_acquire) == seen)
                ++empty_polls;               // one L1 read, about 1 ns
            ++seen;
            record(now_ns() - slot.sent_ns.load());
        }
        // producer side: publish(slot) and nothing else, no system call
        // cost: this core shows 100 percent busy, even with no messages
        std::cout << "messages seen: " << seen << ", empty polls: " << empty_polls << "\n";
    });
    prod.join();
    consumer.join();

    std::sort(lat.begin(), lat.end());
    std::cout << "spinning consumer, wake up latency, this machine:\n";
    std::cout << "  p50 " << percentile(lat, 0.50) << " ns\n";
    std::cout << "  p99 " << percentile(lat, 0.99) << " ns\n";
    std::cout << "  max " << lat.back() << " ns\n";
    std::cout << "No system call and no context switch: the price is the empty polls counter above.\n";
    return lat.size() == static_cast<std::size_t>(kEvents) ? 0 : 1;
}
