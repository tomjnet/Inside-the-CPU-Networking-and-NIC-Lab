// How a NIC Works: RX, TX, DMA and Interrupts - slide 6: descriptor update and the msi-x interrupt
// Build: make 06_descriptor_irq
#include <cstdint>
#include <iostream>

// A model of the last two things the card does for a received packet. No device is touched.
struct RxDescriptor {
    std::uint64_t buffer_addr;
    std::uint16_t length;
    std::uint16_t status;
};

struct Frame { std::uint16_t len; };

struct MsixVector {
    bool masked;        // set by the driver while it polls
    bool pending;       // an interrupt is on its way to the core
    unsigned fired;     // how many interrupts this queue raised
};

constexpr std::uint16_t kDone = 1u << 0;   // descriptor done
constexpr std::uint16_t kEop  = 1u << 1;   // end of packet
constexpr std::uint16_t kCsum = 1u << 2;   // checksum verified

void complete_rx(RxDescriptor& d, const Frame& f, MsixVector& v) {
    d.length = f.len;                  // how many bytes landed
    d.status = kDone | kEop | kCsum;   // one more small DMA write
    if (v.masked) return;              // driver is already polling
    v.pending = true;                  // MSI-X: a PCIe write that the
    ++v.fired;                         // APIC turns into a vector,
}                                      // one vector per queue

static void show(const char* when, const RxDescriptor& d, const MsixVector& v) {
    std::cout << "  " << when << ": length " << d.length << "  done " << ((d.status & kDone) ? 1 : 0)
              << "  eop " << ((d.status & kEop) ? 1 : 0) << "  csum ok " << ((d.status & kCsum) ? 1 : 0)
              << "  | interrupts fired " << v.fired << (v.masked ? "  (vector masked)" : "") << "\n";
}

int main() {
    RxDescriptor ring[3] = {{0x10000000ull, 0, 0}, {0x10000800ull, 0, 0}, {0x10001000ull, 0, 0}};
    MsixVector vec = {false, false, 0};

    std::cout << "packet 1 arrives while the driver sleeps:\n";
    show("before", ring[0], vec);
    complete_rx(ring[0], Frame{1500}, vec);
    show("after ", ring[0], vec);

    // what the hard IRQ handler does first (slide 7): mask the vector and start polling
    vec.masked = true;
    vec.pending = false;

    std::cout << "packets 2 and 3 arrive while the driver polls:\n";
    complete_rx(ring[1], Frame{64}, vec);
    show("after ", ring[1], vec);
    complete_rx(ring[2], Frame{590}, vec);
    show("after ", ring[2], vec);

    std::cout << "3 packets, " << vec.fired << " interrupt: the descriptors carry the news, the vector only wakes the driver\n";

    // correctness: every descriptor is done, and the masked vector stayed silent
    bool all_done = (ring[0].status & kDone) && (ring[1].status & kDone) && (ring[2].status & kDone);
    return (all_done && vec.fired == 1 && !vec.pending) ? 0 : 1;
}
