// NIC Ring Buffers and Descriptor Queues Explained - slide 7: the nic side: fill or drop
// Build: make 07_nic_receive
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

static bool nic_receive(RxRing& r, std::uint16_t len) {
    if (r.head == r.tail) {        // no posted buffer left
        ++r.rx_missed;             // the packet is gone: a drop
        return false;
    }
    RxDescriptor& d = r.desc[r.head & r.mask];
    d.length = len;                // after the DMA to d.buffer_addr
    d.status = kDD;                // last: the driver may read it
    ++r.head;                      // only the NIC writes head
    return true;                   // cost: zero CPU instructions
}

int main() {
    RxRing ring(8);
    for (std::uint32_t i = 0; i < 8; ++i) ring.desc[i].buffer_addr = 0x10000000u + i * 2048u;   // model addresses

    // ten packets arrive and nobody cleans the ring: the driver is away
    int accepted = 0;
    for (int k = 1; k <= 10; ++k) {
        std::uint16_t len = static_cast<std::uint16_t>(60 + k);
        bool ok = nic_receive(ring, len);
        accepted += ok ? 1 : 0;
        std::cout << "packet " << k << " (" << len << " bytes): "
                  << (ok ? "stored" : "DROPPED, no free descriptor") << "  head=" << ring.head
                  << " tail=" << ring.tail << " rx_missed=" << ring.rx_missed << "\n";
    }

    std::cout << "\nthe descriptors as the driver will find them:\n";
    for (std::uint32_t i = 0; i < 8; ++i) {
        const RxDescriptor& d = ring.desc[i];
        std::cout << "  desc[" << i << "] buffer_addr=0x" << std::hex << d.buffer_addr << std::dec
                  << " length=" << d.length << " DD=" << int(d.status & kDD) << "\n";
    }
    std::cout << "accepted " << accepted << ", missed " << ring.rx_missed
              << ": the two drops left no trace except the counter\n";
    return accepted == 8 && ring.rx_missed == 2 ? 0 : 1;
}
