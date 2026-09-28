// How a NIC Works: RX, TX, DMA and Interrupts - slide 7: the driver: a tiny irq handler, then napi poll
// Build: make 07_driver_poll
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

// A model of the driver side of one RX queue. No device is touched.
struct RxDescriptor {
    std::uint64_t buffer_addr;
    std::uint16_t length;
    std::uint16_t status;
};

struct MsixVector {
    bool masked;
    bool pending;
    unsigned fired;
};

struct RxQueue {
    std::vector<RxDescriptor> desc;
    std::size_t next;            // next descriptor the driver will look at
    std::size_t size;
    MsixVector vector;
    std::size_t nic_next;        // next descriptor the NIC will fill
    long long delivered_bytes;
    long long delivered_packets;
};

constexpr std::uint16_t kDone = 1u << 0;

// the sk_buff points at the DMA buffer: the payload is not copied here
static void deliver_to_stack(RxQueue& q, const RxDescriptor& d) {
    q.delivered_bytes += d.length;
    ++q.delivered_packets;
}

// Hard IRQ: mask the vector, schedule the poll, return. Tiny.
void irq_handler(MsixVector& v) { v.masked = true; v.pending = false; }
int napi_poll(RxQueue& q, int budget) {   // softirq: 0 copies
    int done = 0;
    for (; done < budget && (q.desc[q.next].status & kDone); ++done) {
        deliver_to_stack(q, q.desc[q.next]);   // sk_buff wraps it
        q.desc[q.next].status = 0;             // buffer posted again
        q.next = (q.next + 1) % q.size;
    }
    if (done < budget) q.vector.masked = false;   // empty: IRQ on
    return done;
}

// NIC side: complete n packets (slide 6), raise the interrupt only when the vector is not masked
static void nic_receives(RxQueue& q, int n, std::uint16_t len) {
    for (int i = 0; i < n; ++i) {
        RxDescriptor& d = q.desc[q.nic_next];
        d.length = len;
        d.status = kDone;
        q.nic_next = (q.nic_next + 1) % q.size;
        if (!q.vector.masked && !q.vector.pending) { q.vector.pending = true; ++q.vector.fired; }
    }
}

int main() {
    RxQueue q;
    q.size = 256;
    q.desc.assign(q.size, RxDescriptor{0, 0, 0});
    q.next = 0;
    q.vector = MsixVector{false, false, 0};
    q.nic_next = 0;
    q.delivered_bytes = 0;
    q.delivered_packets = 0;

    std::cout << "a burst of 150 packets lands in a ring of " << q.size << " descriptors\n";
    nic_receives(q, 150, 1500);
    std::cout << "  interrupts fired: " << q.vector.fired << "\n";

    irq_handler(q.vector);
    std::cout << "hard IRQ: vector masked, poll scheduled\n";

    int round = 0;
    while (q.vector.masked) {
        int done = napi_poll(q, 64);
        ++round;
        std::cout << "  NAPI poll round " << round << ": " << done << " packets"
                  << (q.vector.masked ? "  (budget used up: poll again, IRQ stays off)" : "  (under budget: IRQ back on)") << "\n";
        if (round == 1) nic_receives(q, 20, 1500);   // more packets arrive while the driver polls: no interrupt
    }

    std::cout << "delivered " << q.delivered_packets << " packets, " << q.delivered_bytes << " bytes, with "
              << q.vector.fired << " interrupt and 0 payload copies in the driver\n";
    std::cout << "under load the driver never unmasks: it is polling, and interrupts stop completely\n";

    // correctness: nothing lost, one interrupt for the whole burst
    return (q.delivered_packets == 170 && q.vector.fired == 1 && q.next == q.nic_next) ? 0 : 1;
}
