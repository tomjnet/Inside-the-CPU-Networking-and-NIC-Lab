// Polling vs Interrupts: Why Low Latency Systems Poll - slide 7: in the lab: a lock free slot
// Build: make 07_slot
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

static std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// one producer, one consumer, one cache line: a lock free slot
struct alignas(64) Slot {
    std::atomic<std::uint64_t> seq{0};      // bumped on every publish
    std::atomic<std::int64_t> sent_ns{0};   // when the producer wrote
};

void publish(Slot& s) {                     // two stores, no lock
    s.sent_ns.store(now_ns(), std::memory_order_relaxed);
    s.seq.fetch_add(1, std::memory_order_release);
}
// the consumer watches seq: a new value means a new message

int main() {
    std::cout << "sizeof(Slot)  = " << sizeof(Slot) << " bytes\n";
    std::cout << "alignof(Slot) = " << alignof(Slot) << " : the slot owns one cache line\n";

    Slot slot;
    constexpr std::uint64_t kMessages = 1000;
    std::atomic<std::uint64_t> acked{0};
    std::uint64_t in_order = 0;

    // the consumer watches seq; when it changes, the timestamp must already be there and must not be older
    std::thread consumer([&] {
        std::int64_t last_sent = 0;
        for (std::uint64_t seen = 0; seen < kMessages; ++seen) {
            while (slot.seq.load(std::memory_order_acquire) == seen) {}
            const std::int64_t sent = slot.sent_ns.load(std::memory_order_relaxed);
            if (sent != 0 && sent >= last_sent) ++in_order;
            last_sent = sent;
            acked.store(seen + 1, std::memory_order_release);
        }
    });
    for (std::uint64_t i = 0; i < kMessages; ++i) {
        publish(slot);
        while (acked.load(std::memory_order_acquire) != i + 1) std::this_thread::yield();
    }
    consumer.join();

    std::cout << "published " << slot.seq.load() << " messages, " << in_order
              << " arrived with their timestamp in place\n";
    std::cout << "publish writes sent_ns first and seq second, so a new seq always has its timestamp.\n";
    return in_order == kMessages ? 0 : 1;
}
