// NIC Ring Buffers and Descriptor Queues Explained - slide 9: a slow consumer: counting drops
// Build: make 09_slow_consumer
#include <cstdint>
#include <iostream>
#include <vector>

struct RxDescriptor {            // slide 3
    std::uint64_t buffer_addr;
    std::uint16_t length;
    std::uint16_t checksum;
    std::uint8_t  status;
    std::uint8_t  errors;
    std::uint16_t vlan;
};
static_assert(sizeof(RxDescriptor) == 16);

constexpr std::uint8_t kDD = 1;  // status bit 0: descriptor done

struct RxRing {                  // slide 6
    std::vector<RxDescriptor> desc;
    std::uint32_t mask;
    std::uint32_t head = 0;
    std::uint32_t tail;
    std::uint32_t clean = 0;
    std::uint64_t rx_missed = 0;
    explicit RxRing(std::uint32_t size)
        : desc(size), mask(size - 1), tail(size) {}
};

static bool nic_receive(RxRing& r, std::uint16_t len) {   // slide 7
    if (r.head == r.tail) {
        ++r.rx_missed;
        return false;
    }
    RxDescriptor& d = r.desc[r.head & r.mask];
    d.length = len;
    d.status = kDD;
    ++r.head;
    return true;
}

static std::uint64_t g_bytes = 0;
static void deliver(std::uint64_t, std::uint16_t length) { g_bytes += length; }

static std::uint32_t driver_poll(RxRing& r, std::uint32_t budget) {   // slide 8
    std::uint32_t done = 0;
    while (done < budget &&
           (r.desc[r.clean & r.mask].status & kDD)) {
        RxDescriptor& d = r.desc[r.clean & r.mask];
        deliver(d.buffer_addr, d.length);
        d.status = 0;
        ++r.clean; ++r.tail; ++done;
    }
    return done;
}

// the loop of the slide with its three numbers as parameters, so the viewer can change them
struct Outcome { std::uint64_t arrived, delivered, missed; };
static Outcome run(std::uint32_t ring_size, int burst, std::uint32_t budget) {
    RxRing ring(ring_size);
    Outcome o{0, 0, 0};
    for (int tick = 0; tick < 1000; ++tick) {
        int arriving = (tick % 10 == 0) ? burst : 2;
        for (int i = 0; i < arriving; ++i) nic_receive(ring, 64);
        o.arrived += static_cast<std::uint64_t>(arriving);
        o.delivered += driver_poll(ring, budget);
    }
    for (std::uint32_t n = driver_poll(ring, budget); n != 0; n = driver_poll(ring, budget)) o.delivered += n;
    o.missed = ring.rx_missed;
    return o;
}

static void print_row(const char* what, const Outcome& o) {
    std::cout << "  " << what << ": arrived " << o.arrived << ", delivered " << o.delivered << ", rx_missed "
              << o.missed << " (" << 100.0 * static_cast<double>(o.missed) / static_cast<double>(o.arrived)
              << " percent)\n";
}

int main() {
    // each tick: packets arrive, then the driver gets its budget
    RxRing ring(8);
    std::uint64_t delivered = 0;
    for (int tick = 0; tick < 1000; ++tick) {
        int arriving = (tick % 10 == 0) ? 12 : 2;  // burst every 10
        for (int i = 0; i < arriving; ++i) nic_receive(ring, 64);
        delivered += driver_poll(ring, 3);         // slow: 3 per tick
    }
    // average in: 3 per tick, the same as the budget, and still
    // drops: a burst of 12 does not fit in 8 descriptors

    std::uint64_t arrived = 100 * 12 + 900 * 2;
    std::uint64_t waiting = ring.head - ring.clean;
    std::cout << "the slide: ring 8, burst 12 every tenth tick, budget 3 per tick\n";
    std::cout << "  arrived " << arrived << " (average " << static_cast<double>(arrived) / 1000.0
              << " per tick), delivered " << delivered << ", still in the ring " << waiting
              << ", rx_missed " << ring.rx_missed << "\n";
    bool balanced = arrived == delivered + waiting + ring.rx_missed;     // every packet is accounted for

    std::cout << "change one number at a time:\n";
    Outcome base = run(8, 12, 3);
    print_row("ring 8,  burst 12, budget 3", base);
    print_row("ring 16, burst 12, budget 3", run(16, 12, 3));
    print_row("ring 8,  burst 12, budget 6", run(8, 12, 6));
    print_row("ring 8,  burst 30, budget 6", run(8, 30, 6));
    print_row("ring 32, burst 30, budget 6", run(32, 30, 6));
    std::cout << "the average rate never exceeded the budget: drops come from the burst against the free descriptors\n";
    std::cout << "bytes delivered to the stack of the model: " << g_bytes << "\n";
    return balanced && base.missed == ring.rx_missed && ring.rx_missed > 0 ? 0 : 1;
}
