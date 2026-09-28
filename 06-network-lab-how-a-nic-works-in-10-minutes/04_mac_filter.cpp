// How a NIC Works: RX, TX, DMA and Interrupts - slide 4: phy, mac and the rx fifo
// Build: make 04_mac_filter
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

// A model of the first two blocks of the card. Nothing here touches a real NIC.
using Mac = std::array<std::uint8_t, 6>;

struct Frame {
    Mac dst;
    std::uint16_t len;      // bytes on the wire
    bool fcs_ok;            // result of the CRC32 check the MAC does in hardware
    const char* note;
};

struct Fifo {
    std::size_t used;
    std::size_t capacity;
    unsigned missed;        // what ethtool -S shows as rx_missed or rx_fifo_errors
};

static const Mac kBroadcast = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// MAC: the first filter, in hardware, before any DMA or CPU work
bool mac_accepts(const Frame& f, const Mac& mine, bool promisc) {
    if (!f.fcs_ok) return false;         // bad CRC32: dropped, counted
    if (promisc) return true;            // tcpdump mode: accept all
    if (f.dst == mine) return true;      // unicast for this port
    return f.dst == kBroadcast;          // ff:ff:ff:ff:ff:ff
}
// RX FIFO: a few hundred KiB on the chip absorb a burst
bool fifo_push(Fifo& q, const Frame& f) {
    if (q.used + f.len > q.capacity) { ++q.missed; return false; }
    q.used += f.len; return true;        // full FIFO: rx_missed grows
}

static unsigned run_burst(Fifo& q, const Frame& f, int frames, int drain_every) {
    unsigned accepted = 0;
    for (int i = 0; i < frames; ++i) {
        if (fifo_push(q, f)) ++accepted;
        // the DMA engine empties one frame every drain_every arrivals (0 = the host is stuck)
        if (drain_every > 0 && i % drain_every == 0 && q.used >= f.len) q.used -= f.len;
    }
    return accepted;
}

int main() {
    const Mac mine = {0x02, 0x00, 0x00, 0xaa, 0xbb, 0x01};
    const Mac other = {0x02, 0x00, 0x00, 0xaa, 0xbb, 0x02};
    const std::vector<Frame> frames = {
        {mine, 1500, true, "unicast for this port"},
        {kBroadcast, 64, true, "broadcast (an ARP request)"},
        {other, 1500, true, "unicast for another port"},
        {mine, 1500, false, "for this port, but the CRC32 is wrong"},
    };

    std::cout << "MAC filter, normal mode and promiscuous mode:\n";
    for (const Frame& f : frames) {
        std::cout << "  " << (mac_accepts(f, mine, false) ? "accept" : "drop  ") << " | "
                  << (mac_accepts(f, mine, true) ? "accept" : "drop  ") << "  " << f.note << "\n";
    }
    std::cout << "  a frame with a bad FCS dies in the MAC in both modes: the CPU never sees it\n\n";

    const Frame full = {mine, 1500, true, "burst"};
    Fifo stuck = {0, 256 * 1024, 0};
    unsigned ok1 = run_burst(stuck, full, 400, 0);
    Fifo slow = {0, 256 * 1024, 0};
    unsigned ok2 = run_burst(slow, full, 400, 2);
    Fifo fast = {0, 256 * 1024, 0};
    unsigned ok3 = run_burst(fast, full, 400, 1);

    std::cout << "RX FIFO of 256 KiB, burst of 400 frames of 1500 bytes:\n";
    std::cout << "  host stuck (no DMA)      accepted " << ok1 << "  rx_missed " << stuck.missed << "\n";
    std::cout << "  DMA drains every 2nd     accepted " << ok2 << "  rx_missed " << slow.missed << "\n";
    std::cout << "  DMA keeps up             accepted " << ok3 << "  rx_missed " << fast.missed << "\n";
    std::cout << "  drops here mean the host side is too slow, not the network\n";

    // correctness: a FIFO that is drained as fast as it fills must not lose a frame
    return (fast.missed == 0 && ok3 == 400) ? 0 : 1;
}
