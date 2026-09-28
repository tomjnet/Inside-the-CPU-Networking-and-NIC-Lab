// How a NIC Works: RX, TX, DMA and Interrupts - slide 9: tx: descriptor, doorbell, dma read
// Build: make 09_tx_doorbell
#include <cstdint>
#include <iostream>
#include <vector>

// A model of one TX queue. The doorbell is a register of the card: here it is a struct that counts
// how many times the driver wrote to it, because each write is one MMIO write across PCIe.
struct TxDescriptor {
    std::uint64_t buffer_addr;
    std::uint16_t length;
    std::uint16_t cmd;
};

struct Doorbell {
    std::uint32_t value = 0;
    unsigned writes = 0;
    Doorbell& operator=(std::uint32_t v) { value = v; ++writes; return *this; }
};

struct TxQueue {
    std::vector<TxDescriptor> desc;
    std::uint32_t tail = 0;      // owned by the driver: next descriptor to fill
    std::uint32_t head = 0;      // owned by the NIC: next descriptor to send
    std::uint32_t size = 0;
    Doorbell doorbell;
};

constexpr std::uint16_t kEop = 1u << 0;         // end of packet
constexpr std::uint16_t kInsertFcs = 1u << 1;   // the MAC appends the CRC32

void transmit(TxQueue& q, std::uint64_t bus_addr, std::uint16_t n) {
    TxDescriptor& d = q.desc[q.tail];
    d.buffer_addr = bus_addr;        // where the frame sits in RAM
    d.length = n;                    // the NIC will DMA read n bytes
    d.cmd = kEop | kInsertFcs;       // the MAC appends the CRC32
    q.tail = (q.tail + 1) % q.size;
    q.doorbell = q.tail;             // MMIO write: 1 PCIe crossing
}   // batch several descriptors, ring the doorbell once

// the batched variant: fill every descriptor first, one doorbell at the end (what xmit_more achieves)
static void transmit_batch(TxQueue& q, std::uint64_t first_addr, std::uint16_t n, int count) {
    for (int i = 0; i < count; ++i) {
        TxDescriptor& d = q.desc[q.tail];
        d.buffer_addr = first_addr + static_cast<std::uint64_t>(i) * 2048u;
        d.length = n;
        d.cmd = kEop | kInsertFcs;
        q.tail = (q.tail + 1) % q.size;
    }
    q.doorbell = q.tail;
}

// NIC side: everything between head and the doorbell value is work. Two DMA reads per packet.
static long long nic_sends(TxQueue& q, bool print) {
    long long wire_bytes = 0;
    while (q.head != q.doorbell.value) {
        const TxDescriptor& d = q.desc[q.head];
        long long fcs = (d.cmd & kInsertFcs) ? 4 : 0;
        if (print) {
            std::cout << "  NIC: DMA read descriptor " << q.head << ", DMA read " << d.length << " bytes at 0x" << std::hex
                      << d.buffer_addr << std::dec << ", +" << fcs << " bytes FCS, on the wire\n";
        }
        wire_bytes += d.length + fcs;
        q.head = (q.head + 1) % q.size;
    }
    return wire_bytes;
}

int main() {
    TxQueue one;
    one.size = 64;
    one.desc.assign(one.size, TxDescriptor{0, 0, 0});

    std::cout << "one packet: descriptor, doorbell, then the card does the rest\n";
    transmit(one, 0x20000000ull, 1500);
    std::cout << "  driver: descriptor 0 filled, doorbell = " << one.doorbell.value << "\n";
    long long sent = nic_sends(one, true);
    std::cout << "  " << sent << " bytes left the MAC; later a completion interrupt lets the driver free the sk_buff\n\n";

    TxQueue a;
    a.size = 64;
    a.desc.assign(a.size, TxDescriptor{0, 0, 0});
    for (std::uint64_t i = 0; i < 32; ++i) transmit(a, 0x20000000ull + i * 2048u, 1500);
    long long bytes_a = nic_sends(a, false);

    TxQueue b;
    b.size = 64;
    b.desc.assign(b.size, TxDescriptor{0, 0, 0});
    for (int batch = 0; batch < 4; ++batch) transmit_batch(b, 0x20000000ull + static_cast<std::uint64_t>(batch) * 16384u, 1500, 8);
    long long bytes_b = nic_sends(b, false);

    const double doorbell_ns = 100.0;   // typical cost of one MMIO write for the CPU, not a measurement
    std::cout << "32 packets, one doorbell each : " << a.doorbell.writes << " MMIO writes, about "
              << a.doorbell.writes * doorbell_ns << " ns of CPU time (typical)\n";
    std::cout << "32 packets, batches of 8      : " << b.doorbell.writes << " MMIO writes, about "
              << b.doorbell.writes * doorbell_ns << " ns of CPU time (typical)\n";
    std::cout << "same bytes on the wire: " << bytes_a << " and " << bytes_b << "\n";
    std::cout << "batching saves CPU time, but the first packet of a batch waits for the last: latency against throughput\n";

    // correctness: both ways send the same bytes, and the batch rings 8 times less
    return (bytes_a == bytes_b && a.doorbell.writes == 32 && b.doorbell.writes == 4) ? 0 : 1;
}
