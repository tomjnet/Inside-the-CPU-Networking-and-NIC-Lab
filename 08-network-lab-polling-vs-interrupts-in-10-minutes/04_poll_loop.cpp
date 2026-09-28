// Polling vs Interrupts: Why Low Latency Systems Poll - slide 4: a core that spins on the ring
// Build: make 04_poll_loop
#include <cstdint>
#include <iostream>

// A model of the RX ring of episode 7: the NIC moves head, the driver moves tail, indexes are masked.
constexpr std::uint64_t kSize = 8;
constexpr std::uint64_t kMask = kSize - 1;
constexpr std::uint64_t kPasses = 1000000;    // the demo is bounded: a real poller never stops
constexpr std::uint64_t kEvery = 1000;        // the model NIC delivers one packet every 1000 passes

struct Packet { std::uint64_t id; };
struct Ring {
    Packet slots[kSize]{};
    std::uint64_t head = 0;                   // written by the NIC
    std::uint64_t tail = 0;                   // written by the driver
};

static std::uint64_t g_sum = 0;
static void handle(const Packet& p) { g_sum += p.id; }

// The model NIC: now and then it writes a packet at head, as DMA would. Returns false when the demo ends.
static bool nic_model_step(Ring& ring, std::uint64_t polls) {
    if (polls >= kPasses) return false;
    if (polls % kEvery == kEvery - 1 && ring.head - ring.tail < kSize) {
        ring.slots[ring.head & kMask] = Packet{ring.head + 1};
        ++ring.head;
    }
    return true;
}

int main() {
    Ring ring;

    // one core, one RX ring, no interrupt: the driver keeps asking
    std::uint64_t polls = 0, empty = 0, packets = 0;
    while (nic_model_step(ring, polls)) {    // the NIC moves head
        ++polls;
        if (ring.head == ring.tail) {        // one read of a cached line
            ++empty;                         // nothing yet: ask again
            continue;
        }
        handle(ring.slots[ring.tail & kMask]);   // seen within one pass
        ++ring.tail;                         // no wake up, no switch
        ++packets;
    }

    std::cout << "passes of the loop : " << polls << "\n";
    std::cout << "empty passes       : " << empty << "\n";
    std::cout << "packets handled    : " << packets << "\n";
    std::cout << "empty per packet   : " << empty / packets << "\n";
    std::cout << "Every packet was seen in the first pass after head moved: no interrupt, no wake up.\n";
    std::cout << "The price is the first number: the core never stopped asking.\n";

    const std::uint64_t expected = kPasses / kEvery;
    const bool ok = packets == expected && g_sum == expected * (expected + 1) / 2 && ring.head == ring.tail;
    std::cout << (ok ? "check: every packet handled once\n" : "check FAILED: a packet was lost\n");
    return ok ? 0 : 1;
}
