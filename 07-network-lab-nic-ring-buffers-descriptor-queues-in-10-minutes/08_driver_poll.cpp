// NIC Ring Buffers and Descriptor Queues Explained - slide 8: the driver side: clean and refill
// Build: make 08_driver_poll
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

// the network stack of the model: it only counts what it was given
static std::uint64_t g_packets = 0, g_bytes = 0;
static void deliver(std::uint64_t buffer_addr, std::uint16_t length) {
    ++g_packets;
    g_bytes += length;
    std::cout << "    delivered " << length << " bytes from buffer 0x" << std::hex << buffer_addr << std::dec << "\n";
}

// driver: read finished packets, hand the buffers back
static std::uint32_t driver_poll(RxRing& r, std::uint32_t budget) {
    std::uint32_t done = 0;
    while (done < budget &&
           (r.desc[r.clean & r.mask].status & kDD)) {
        RxDescriptor& d = r.desc[r.clean & r.mask];
        deliver(d.buffer_addr, d.length);   // up the stack
        d.status = 0;                       // an empty buffer again
        ++r.clean; ++r.tail; ++done;        // only the driver writes
    }
    return done;      // no PCIe read: the status lives in RAM
}

int main() {
    RxRing ring(8);
    for (std::uint32_t i = 0; i < 8; ++i) ring.desc[i].buffer_addr = 0x10000000u + i * 2048u;   // model addresses

    for (int k = 1; k <= 5; ++k) nic_receive(ring, static_cast<std::uint16_t>(100 * k));
    std::cout << "5 packets arrived: head=" << ring.head << " tail=" << ring.tail << " clean=" << ring.clean << "\n";

    std::uint32_t polls = 0, total = 0;
    for (;;) {
        std::cout << "  driver_poll(budget 3):\n";
        std::uint32_t n = driver_poll(ring, 3);
        ++polls;
        total += n;
        std::cout << "  cleaned " << n << ": head=" << ring.head << " tail=" << ring.tail << " clean=" << ring.clean
                  << " free buffers=" << (ring.tail - ring.head) << "\n";
        if (n < 3) break;          // fewer than the budget: the ring is empty, which is how NAPI decides to stop
    }

    // 7 more packets: the ring wraps, the driver keeps up, nothing is missed
    for (int k = 6; k <= 12; ++k) nic_receive(ring, 64);
    std::cout << "7 more arrived, the indexes wrapped: head & mask=" << (ring.head & ring.mask) << "\n";
    std::cout << "  driver_poll(budget 64):\n";
    total += driver_poll(ring, 64);
    ++polls;
    std::cout << "packets delivered " << g_packets << ", bytes " << g_bytes << ", rx_missed " << ring.rx_missed << "\n";
    std::cout << "tail register writes: " << total << " if the driver rings the doorbell per packet, " << polls
              << " if it writes once per poll: real drivers batch, because each write crosses PCIe\n";
    bool ok = g_packets == 12 && ring.rx_missed == 0 && ring.head == ring.clean && ring.tail - ring.head == 8;
    return ok ? 0 : 1;
}
