// NIC Ring Buffers and Descriptor Queues Explained - slide 6: the ring as a c++ model
// Build: make 06_ring_model
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

struct RxRing {
    std::vector<RxDescriptor> desc;  // shared by NIC and driver
    std::uint32_t mask;              // size - 1, size a power of two
    std::uint32_t head = 0;          // NIC: next descriptor to fill
    std::uint32_t tail;              // driver: end of posted buffers
    std::uint32_t clean = 0;         // driver: next packet to read
    std::uint64_t rx_missed = 0;     // arrivals with no free buffer
    explicit RxRing(std::uint32_t size)
        : desc(size), mask(size - 1), tail(size) {}
};
// free running counters: desc[head & mask] does the wrap
// full: head == tail, every posted buffer holds a packet

static bool is_power_of_two(std::uint32_t n) { return n != 0 && (n & (n - 1)) == 0; }

static void print_state(const char* when, const RxRing& r) {
    std::cout << "  " << when << ": head=" << r.head << " tail=" << r.tail << " clean=" << r.clean
              << " | free buffers (tail - head)=" << (r.tail - r.head)
              << " | packets waiting (head - clean)=" << (r.head - r.clean)
              << (r.head == r.tail ? " | FULL" : "") << "\n";
}

int main() {
    const std::uint32_t size = 8;
    if (!is_power_of_two(size)) return 1;          // the mask only works for a power of two
    RxRing ring(size);
    std::cout << "ring of " << ring.desc.size() << " descriptors, " << ring.desc.size() * sizeof(RxDescriptor)
              << " bytes, mask=" << ring.mask << "\n";
    print_state("start        ", ring);

    std::cout << "the wrap, counter & mask:";
    for (std::uint32_t c = 5; c < 12; ++c) std::cout << " " << c << "->" << (c & ring.mask);
    std::cout << "\n";

    ring.head += 5;                                // the NIC filled 5 descriptors
    print_state("NIC filled 5 ", ring);
    ring.clean += 3; ring.tail += 3;               // the driver read 3 packets and posted the buffers again
    print_state("driver did 3 ", ring);
    ring.head += 6;                                // 6 more arrivals: the last free buffer is used
    print_state("NIC filled 6 ", ring);
    bool full_seen = ring.head == ring.tail;

    // free running counters survive the 32 bit overflow: unsigned subtraction is modulo 2^32
    RxRing old(size);
    old.head = 0xFFFFFFFCu; old.clean = old.head; old.tail = old.head + size;   // tail has already wrapped to 4
    std::cout << "near the 32 bit overflow:\n";
    print_state("before       ", old);
    old.head += 6;                                 // head wraps past zero too
    print_state("6 arrivals   ", old);
    bool overflow_ok = (old.tail - old.head) == 2 && (old.head - old.clean) == 6;

    std::cout << "real hardware keeps wrapped indexes in its head and tail registers and leaves one slot empty;\n"
                 "the model keeps free running counters, so all " << size << " descriptors are usable\n";
    return full_seen && overflow_ok ? 0 : 1;
}
