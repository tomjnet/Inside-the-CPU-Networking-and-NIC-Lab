// How a NIC Works: RX, TX, DMA and Interrupts - slide 5: dma: the nic writes into host memory
// Build: make 05_dma_write
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

// A model: "host RAM" is a vector, a "bus address" is an offset from kBusBase. No device is touched.
constexpr std::uint64_t kBusBase = 0x10000000ull;
constexpr std::size_t kBufferSize = 2048;

struct HostMemory {
    std::vector<std::uint8_t> bytes;
    std::uint8_t* at(std::uint64_t bus_addr) { return bytes.data() + (bus_addr - kBusBase); }
};

struct Frame {
    std::vector<std::uint8_t> bytes;
    std::uint16_t len;
};

// The driver posted this earlier: "put the next packet here"
struct RxDescriptor {
    std::uint64_t buffer_addr;   // bus address of a 2 KiB buffer
    std::uint16_t length;        // filled in by the NIC
    std::uint16_t status;        // filled in by the NIC: bit 0 = done
};
// NIC side: the frame lands in host RAM, zero CPU instructions
void dma_write(HostMemory& ram, const RxDescriptor& d,
               const Frame& f) {
    std::memcpy(ram.at(d.buffer_addr), f.bytes.data(), f.len);
}   // real hardware: PCIe memory writes, 1 crossing, 0 CPU copies

int main() {
    // driver, at start up: allocate 4 buffers, "map" them, write one descriptor per buffer
    HostMemory ram;
    ram.bytes.assign(4 * kBufferSize, 0);
    std::vector<RxDescriptor> ring;
    for (std::uint64_t i = 0; i < 4; ++i) ring.push_back(RxDescriptor{kBusBase + i * kBufferSize, 0, 0});

    std::cout << "sizeof(RxDescriptor) = " << sizeof(RxDescriptor) << " bytes (address, length, status, padding)\n";
    std::cout << "driver posted " << ring.size() << " buffers of " << kBufferSize << " bytes:\n";
    for (const RxDescriptor& d : ring) {
        std::cout << "  descriptor -> bus address 0x" << std::hex << d.buffer_addr << std::dec
                  << "  length " << d.length << "  status " << d.status << "\n";
    }

    // NIC, later: a frame is ready in the RX FIFO, take the next free descriptor and write
    Frame f;
    f.len = 64;
    for (std::uint16_t i = 0; i < f.len; ++i) f.bytes.push_back(static_cast<std::uint8_t>(0xa0 + i % 16));
    const RxDescriptor& next = ring[1];
    dma_write(ram, next, f);

    std::cout << "\nNIC wrote " << f.len << " bytes by DMA into buffer 1. Host RAM at that bus address:\n  ";
    const std::uint8_t* p = ram.at(next.buffer_addr);
    for (int i = 0; i < 16; ++i) std::cout << std::hex << std::setw(2) << std::setfill('0') << int(p[i]) << ' ';
    std::cout << std::dec << "\n";
    std::cout << "buffer 0 is untouched: first byte " << int(*ram.at(ring[0].buffer_addr)) << "\n";
    std::cout << "copies made by the CPU: 0 (in this model the memcpy plays the card, not the CPU)\n";
    std::cout << "the descriptor still says length 0, status 0: nobody knows yet, see 06_descriptor_irq\n";

    // correctness: every byte of the frame must be in the posted buffer, and nowhere else
    bool same = std::memcmp(ram.at(next.buffer_addr), f.bytes.data(), f.len) == 0;
    return (same && *ram.at(ring[0].buffer_addr) == 0) ? 0 : 1;
}
