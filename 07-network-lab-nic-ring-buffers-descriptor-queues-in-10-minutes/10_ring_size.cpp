// NIC Ring Buffers and Descriptor Queues Explained - slide 10: ring size: latency against loss
// Build: make 10_ring_size
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iomanip>
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

// the packet buffers of the model hold one thing: the tick the packet arrived in.
// buffer_addr is the index of the buffer, so the "DMA write" is one store into this vector.
static std::vector<std::uint64_t> g_buffers;
static std::uint64_t g_now = 0;          // the current tick
static std::uint64_t g_max_wait = 0;     // longest time a delivered packet sat in the ring, in ticks
static std::uint64_t g_wait_sum = 0;
static std::uint64_t g_delivered = 0;

static bool nic_receive(RxRing& r, std::uint16_t len) {   // slide 7, plus the arrival stamp
    if (r.head == r.tail) {
        ++r.rx_missed;
        return false;
    }
    RxDescriptor& d = r.desc[r.head & r.mask];
    g_buffers[static_cast<std::size_t>(d.buffer_addr)] = g_now;      // the DMA write
    d.length = len;
    d.status = kDD;
    ++r.head;
    return true;
}

static void deliver(std::uint64_t buffer_addr, std::uint16_t) {
    std::uint64_t wait = g_now - g_buffers[static_cast<std::size_t>(buffer_addr)];
    if (wait > g_max_wait) g_max_wait = wait;
    g_wait_sum += wait;
    ++g_delivered;
}

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

struct Result { std::uint64_t rx_missed, max_wait_ticks, delivered; double mean_wait_ticks; };

// 1000 ticks: 2 packets per tick, a burst every tenth tick, then the driver cleans up to its budget
static Result run_traffic(std::uint32_t size, int burst, std::uint32_t budget) {
    RxRing ring(size);
    g_buffers.assign(size, 0);
    for (std::uint32_t i = 0; i < size; ++i) ring.desc[i].buffer_addr = i;
    g_max_wait = 0; g_wait_sum = 0; g_delivered = 0;
    for (g_now = 0; g_now < 1000; ++g_now) {
        int arriving = (g_now % 10 == 0) ? burst : 2;
        for (int i = 0; i < arriving; ++i) nic_receive(ring, 64);
        driver_poll(ring, budget);
    }
    for (; ring.head != ring.clean; ++g_now) driver_poll(ring, budget);      // drain what is left
    double mean = g_delivered ? static_cast<double>(g_wait_sum) / static_cast<double>(g_delivered) : 0.0;
    return Result{ring.rx_missed, g_max_wait, g_delivered, mean};
}

static void print_row(std::uint32_t size, std::uint64_t rx_missed, std::uint64_t max_wait_ticks) {
    std::cout << "  ring " << std::setw(4) << size << ": rx_missed " << std::setw(6) << rx_missed
              << "   worst wait " << std::setw(3) << max_wait_ticks << " ticks\n";
}

int main() {
    std::cout << "burst of 200 every tenth tick, 2 per tick otherwise (average 21.8), budget 22 per tick\n";
    std::uint64_t first_missed = 0, last_missed = 0, first_wait = 0, last_wait = 0;
    // same traffic, four ring sizes: drops against the worst wait
    for (std::uint32_t size : {8u, 32u, 128u, 512u}) {
        Result r = run_traffic(size, /*burst=*/200, /*budget=*/22);
        print_row(size, r.rx_missed, r.max_wait_ticks);
        std::cout << "             delivered " << r.delivered << ", mean wait " << r.mean_wait_ticks << " ticks, "
                  << size * sizeof(RxDescriptor) << " bytes of descriptors\n";
        if (size == 8u) { first_missed = r.rx_missed; first_wait = r.max_wait_ticks; }
        last_missed = r.rx_missed; last_wait = r.max_wait_ticks;
    }
    // small ring: drops in every burst, almost no waiting
    // large ring: no drops, the end of a burst waits the longest
    // latency against loss: size the ring for your worst burst

    std::cout << "a tick is one poll of the driver: on real hardware tens of microseconds to a millisecond\n";
    std::cout << "the ring is a buffer: it turned " << first_missed << " lost packets into a worst wait of "
              << last_wait << " ticks\n";
    bool ok = first_missed > 0 && last_missed == 0 && last_wait > first_wait;   // the trade itself, not a timing
    return ok ? 0 : 1;
}
