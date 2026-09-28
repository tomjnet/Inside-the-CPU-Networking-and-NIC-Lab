// NIC Ring Buffers and Descriptor Queues Explained - slide 3: the descriptor: 16 bytes
// Build: make 03_descriptor
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

struct RxDescriptor {            // 16 bytes: 4 per cache line
    std::uint64_t buffer_addr;   // driver: where DMA may write
    std::uint16_t length;        // NIC: bytes of the packet
    std::uint16_t checksum;      // NIC: computed in hardware
    std::uint8_t  status;        // NIC: bit 0 is DD, descriptor done
    std::uint8_t  errors;        // NIC: CRC error, too long
    std::uint16_t vlan;          // NIC: the stripped VLAN tag
};
static_assert(sizeof(RxDescriptor) == 16);

// a model: the layout follows the classic Intel legacy receive descriptor, no device is touched
constexpr std::uint8_t kDD = 1;                     // descriptor done
static unsigned char packet_buffer[2048];           // the buffer the descriptor points at

static void print_field(const char* name, std::size_t offset, std::size_t size, const char* writer) {
    std::cout << "  " << std::left << std::setw(12) << name << " offset " << std::setw(3) << offset
              << " size " << std::setw(2) << size << " written by the " << writer << "\n";
}

int main() {
    std::cout << "sizeof(RxDescriptor) = " << sizeof(RxDescriptor) << " bytes, "
              << 64 / sizeof(RxDescriptor) << " per 64 byte cache line\n";
    print_field("buffer_addr", offsetof(RxDescriptor, buffer_addr), sizeof(std::uint64_t), "driver");
    print_field("length", offsetof(RxDescriptor, length), sizeof(std::uint16_t), "NIC");
    print_field("checksum", offsetof(RxDescriptor, checksum), sizeof(std::uint16_t), "NIC");
    print_field("status", offsetof(RxDescriptor, status), sizeof(std::uint8_t), "NIC");
    print_field("errors", offsetof(RxDescriptor, errors), sizeof(std::uint8_t), "NIC");
    print_field("vlan", offsetof(RxDescriptor, vlan), sizeof(std::uint16_t), "NIC");

    // the driver posts an empty buffer: only the address is set
    RxDescriptor d{};
    d.buffer_addr = reinterpret_cast<std::uintptr_t>(packet_buffer);
    std::cout << "\nposted:   buffer_addr=0x" << std::hex << d.buffer_addr << std::dec
              << " status=" << int(d.status) << " (DD clear: the buffer is empty)\n";

    // the "NIC": copies a packet into the buffer (the DMA write), then fills in the descriptor
    const char wire[] = "64 bytes of market data would be here";
    std::memcpy(packet_buffer, wire, sizeof(wire));
    d.length = static_cast<std::uint16_t>(sizeof(wire));
    d.status = kDD;
    std::cout << "received: length=" << d.length << " status=" << int(d.status)
              << " errors=" << int(d.errors) << " vlan=" << d.vlan << " checksum=" << d.checksum
              << " (DD set: the driver may read)\n";

    // the driver reads the packet through the address: the packet itself never moved
    const unsigned char* p = reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(d.buffer_addr));
    std::cout << "driver reads " << d.length << " bytes at buffer_addr: \""
              << reinterpret_cast<const char*>(p) << "\"\n";

    std::cout << "\na ring of 512 descriptors = " << 512 * sizeof(RxDescriptor) << " bytes of descriptors, plus "
              << 512 * sizeof(packet_buffer) / 1024 << " KiB of packet buffers\n";
    bool ok = (d.status & kDD) != 0 && std::memcmp(p, wire, sizeof(wire)) == 0;
    return ok ? 0 : 1;
}
