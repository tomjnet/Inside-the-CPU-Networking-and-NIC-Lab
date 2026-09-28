/* How a NIC Works: RX, TX, DMA and Interrupts - slide 5: dma: the nic writes into host memory (C version of 05_dma_write.cpp) */
/* Build: make 05_dma_write_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A model: "host RAM" is a heap block, a "bus address" is an offset from kBusBase. No device is touched. */
#define kBusBase 0x10000000ull
#define kBufferSize 2048u
#define kBuffers 4u

/* C has no std::vector: a pointer plus a length, freed by the owner */
typedef struct {
    uint8_t *bytes;
    size_t size;
} HostMemory;
static uint8_t *ram_at(HostMemory *ram, uint64_t bus_addr) { return ram->bytes + (size_t)(bus_addr - kBusBase); }

typedef struct {
    uint8_t bytes[kBufferSize];
    uint16_t len;
} Frame;

/* The driver posted this earlier: "put the next packet here" */
typedef struct {
    uint64_t buffer_addr;   /* bus address of a 2 KiB buffer */
    uint16_t length;        /* filled in by the NIC */
    uint16_t status;        /* filled in by the NIC: bit 0 = done */
} RxDescriptor;
/* NIC side: the frame lands in host RAM, zero CPU instructions */
static void dma_write(HostMemory *ram, const RxDescriptor *d,
                      const Frame *f) {
    memcpy(ram_at(ram, d->buffer_addr), f->bytes, f->len);
}   /* real hardware: PCIe memory writes, 1 crossing, 0 CPU copies */

int main(void) {
    /* driver, at start up: allocate 4 buffers, "map" them, write one descriptor per buffer */
    HostMemory ram;
    ram.size = kBuffers * kBufferSize;
    ram.bytes = calloc(ram.size, 1);
    if (ram.bytes == NULL) { printf("out of memory\n"); return 1; }
    RxDescriptor ring[kBuffers];
    for (uint64_t i = 0; i < kBuffers; ++i) {
        RxDescriptor d = {kBusBase + i * kBufferSize, 0, 0};
        ring[i] = d;
    }

    printf("sizeof(RxDescriptor) = %zu bytes (address, length, status, padding)\n", sizeof(RxDescriptor));
    printf("driver posted %u buffers of %u bytes:\n", kBuffers, kBufferSize);
    for (size_t i = 0; i < kBuffers; ++i) {
        const RxDescriptor *d = &ring[i];
        printf("  descriptor -> bus address 0x%" PRIx64 "  length %u  status %u\n",
               d->buffer_addr, (unsigned)d->length, (unsigned)d->status);
    }

    /* NIC, later: a frame is ready in the RX FIFO, take the next free descriptor and write */
    static Frame f;
    f.len = 64;
    for (uint16_t i = 0; i < f.len; ++i) f.bytes[i] = (uint8_t)(0xa0 + i % 16);
    const RxDescriptor *next = &ring[1];
    dma_write(&ram, next, &f);

    printf("\nNIC wrote %u bytes by DMA into buffer 1. Host RAM at that bus address:\n  ", (unsigned)f.len);
    const uint8_t *p = ram_at(&ram, next->buffer_addr);
    for (int i = 0; i < 16; ++i) printf("%02x ", (unsigned)p[i]);
    printf("\n");
    printf("buffer 0 is untouched: first byte %d\n", (int)*ram_at(&ram, ring[0].buffer_addr));
    printf("copies made by the CPU: 0 (in this model the memcpy plays the card, not the CPU)\n");
    printf("the descriptor still says length 0, status 0: nobody knows yet, see 06_descriptor_irq\n");

    /* correctness: every byte of the frame must be in the posted buffer, and nowhere else */
    int same = memcmp(ram_at(&ram, next->buffer_addr), f.bytes, f.len) == 0;
    int ok = same && *ram_at(&ram, ring[0].buffer_addr) == 0;
    free(ram.bytes);   /* C has no destructor: the owner frees */
    return ok ? 0 : 1;
}
