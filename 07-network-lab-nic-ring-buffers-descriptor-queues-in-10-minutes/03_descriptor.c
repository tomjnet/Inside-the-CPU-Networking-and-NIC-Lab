/* NIC Ring Buffers and Descriptor Queues Explained - slide 3: the descriptor: 16 bytes (C version of 03_descriptor.cpp) */
/* Build: make 03_descriptor_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {                 /* 16 bytes: 4 per cache line */
    uint64_t buffer_addr;        /* driver: where DMA may write */
    uint16_t length;             /* NIC: bytes of the packet */
    uint16_t checksum;           /* NIC: computed in hardware */
    uint8_t  status;             /* NIC: bit 0 is DD, descriptor done */
    uint8_t  errors;             /* NIC: CRC error, too long */
    uint16_t vlan;               /* NIC: the stripped VLAN tag */
} RxDescriptor;
_Static_assert(sizeof(RxDescriptor) == 16, "a descriptor is 16 bytes");

/* a model: the layout follows the classic Intel legacy receive descriptor, no device is touched */
enum { kDD = 1 };                                   /* descriptor done */
static unsigned char packet_buffer[2048];           /* the buffer the descriptor points at */

static void print_field(const char *name, size_t offset, size_t size, const char *writer) {
    printf("  %-12s offset %-3zu size %-2zu written by the %s\n", name, offset, size, writer);
}

int main(void) {
    printf("sizeof(RxDescriptor) = %zu bytes, %zu per 64 byte cache line\n",
           sizeof(RxDescriptor), 64 / sizeof(RxDescriptor));
    print_field("buffer_addr", offsetof(RxDescriptor, buffer_addr), sizeof(uint64_t), "driver");
    print_field("length", offsetof(RxDescriptor, length), sizeof(uint16_t), "NIC");
    print_field("checksum", offsetof(RxDescriptor, checksum), sizeof(uint16_t), "NIC");
    print_field("status", offsetof(RxDescriptor, status), sizeof(uint8_t), "NIC");
    print_field("errors", offsetof(RxDescriptor, errors), sizeof(uint8_t), "NIC");
    print_field("vlan", offsetof(RxDescriptor, vlan), sizeof(uint16_t), "NIC");

    /* the driver posts an empty buffer: only the address is set */
    RxDescriptor d = {0};
    d.buffer_addr = (uint64_t)(uintptr_t)packet_buffer;
    printf("\nposted:   buffer_addr=0x%" PRIx64 " status=%d (DD clear: the buffer is empty)\n",
           d.buffer_addr, (int)d.status);

    /* the "NIC": copies a packet into the buffer (the DMA write), then fills in the descriptor */
    const char wire[] = "64 bytes of market data would be here";
    memcpy(packet_buffer, wire, sizeof(wire));
    d.length = (uint16_t)sizeof(wire);
    d.status = kDD;
    printf("received: length=%u status=%d errors=%d vlan=%u checksum=%u (DD set: the driver may read)\n",
           (unsigned)d.length, (int)d.status, (int)d.errors, (unsigned)d.vlan, (unsigned)d.checksum);

    /* the driver reads the packet through the address: the packet itself never moved */
    const unsigned char *p = (const unsigned char *)(uintptr_t)d.buffer_addr;
    printf("driver reads %u bytes at buffer_addr: \"%s\"\n", (unsigned)d.length, (const char *)p);

    printf("\na ring of 512 descriptors = %zu bytes of descriptors, plus %zu KiB of packet buffers\n",
           512 * sizeof(RxDescriptor), 512 * sizeof(packet_buffer) / 1024);
    bool ok = (d.status & kDD) != 0 && memcmp(p, wire, sizeof(wire)) == 0;
    return ok ? 0 : 1;
}
