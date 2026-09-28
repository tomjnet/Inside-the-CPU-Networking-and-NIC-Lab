/* How a NIC Works: RX, TX, DMA and Interrupts - slide 9: tx: descriptor, doorbell, dma read (C version of 09_tx_doorbell.cpp) */
/* Build: make 09_tx_doorbell_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* A model of one TX queue. The doorbell is a register of the card: here it is a struct that counts
   how many times the driver wrote to it, because each write is one MMIO write across PCIe. */
typedef struct {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t cmd;
} TxDescriptor;

typedef struct {
    uint32_t value;
    unsigned writes;
} Doorbell;
/* C has no operator=: every doorbell write goes through this function */
static void ring_doorbell(Doorbell *db, uint32_t v) { db->value = v; ++db->writes; }

typedef struct {
    TxDescriptor *desc;          /* C has no std::vector: calloc plus size, freed by the owner */
    uint32_t tail;               /* owned by the driver: next descriptor to fill */
    uint32_t head;               /* owned by the NIC: next descriptor to send */
    uint32_t size;
    Doorbell doorbell;
} TxQueue;

enum {
    kEop = 1u << 0,         /* end of packet */
    kInsertFcs = 1u << 1    /* the MAC appends the CRC32 */
};

static bool queue_init(TxQueue *q, uint32_t size) {
    q->desc = calloc(size, sizeof *q->desc);
    q->tail = 0;
    q->head = 0;
    q->size = size;
    q->doorbell.value = 0;
    q->doorbell.writes = 0;
    return q->desc != NULL;
}

static void transmit(TxQueue *q, uint64_t bus_addr, uint16_t n) {
    TxDescriptor *d = &q->desc[q->tail];
    d->buffer_addr = bus_addr;              /* where the frame sits in RAM */
    d->length = n;                          /* the NIC will DMA read n bytes */
    d->cmd = (uint16_t)(kEop | kInsertFcs); /* the MAC appends the CRC32 */
    q->tail = (q->tail + 1) % q->size;
    ring_doorbell(&q->doorbell, q->tail);   /* MMIO write: 1 PCIe crossing */
}   /* batch several descriptors, ring the doorbell once */

/* the batched variant: fill every descriptor first, one doorbell at the end (what xmit_more achieves) */
static void transmit_batch(TxQueue *q, uint64_t first_addr, uint16_t n, int count) {
    for (int i = 0; i < count; ++i) {
        TxDescriptor *d = &q->desc[q->tail];
        d->buffer_addr = first_addr + (uint64_t)i * 2048u;
        d->length = n;
        d->cmd = (uint16_t)(kEop | kInsertFcs);
        q->tail = (q->tail + 1) % q->size;
    }
    ring_doorbell(&q->doorbell, q->tail);
}

/* NIC side: everything between head and the doorbell value is work. Two DMA reads per packet. */
static long long nic_sends(TxQueue *q, bool print) {
    long long wire_bytes = 0;
    while (q->head != q->doorbell.value) {
        const TxDescriptor *d = &q->desc[q->head];
        long long fcs = (d->cmd & kInsertFcs) ? 4 : 0;
        if (print) {
            printf("  NIC: DMA read descriptor %" PRIu32 ", DMA read %u bytes at 0x%" PRIx64 ", +%lld bytes FCS, on the wire\n",
                   q->head, (unsigned)d->length, d->buffer_addr, fcs);
        }
        wire_bytes += d->length + fcs;
        q->head = (q->head + 1) % q->size;
    }
    return wire_bytes;
}

int main(void) {
    TxQueue one, a, b;
    one.desc = a.desc = b.desc = NULL;
    int rc = 1;
    if (!queue_init(&one, 64) || !queue_init(&a, 64) || !queue_init(&b, 64)) {
        printf("out of memory\n");
        goto done;
    }

    printf("one packet: descriptor, doorbell, then the card does the rest\n");
    transmit(&one, 0x20000000ull, 1500);
    printf("  driver: descriptor 0 filled, doorbell = %" PRIu32 "\n", one.doorbell.value);
    long long sent = nic_sends(&one, true);
    printf("  %lld bytes left the MAC; later a completion interrupt lets the driver free the sk_buff\n\n", sent);

    for (uint64_t i = 0; i < 32; ++i) transmit(&a, 0x20000000ull + i * 2048u, 1500);
    long long bytes_a = nic_sends(&a, false);

    for (int batch = 0; batch < 4; ++batch) transmit_batch(&b, 0x20000000ull + (uint64_t)batch * 16384u, 1500, 8);
    long long bytes_b = nic_sends(&b, false);

    const double doorbell_ns = 100.0;   /* typical cost of one MMIO write for the CPU, not a measurement */
    printf("32 packets, one doorbell each : %u MMIO writes, about %g ns of CPU time (typical)\n",
           a.doorbell.writes, a.doorbell.writes * doorbell_ns);
    printf("32 packets, batches of 8      : %u MMIO writes, about %g ns of CPU time (typical)\n",
           b.doorbell.writes, b.doorbell.writes * doorbell_ns);
    printf("same bytes on the wire: %lld and %lld\n", bytes_a, bytes_b);
    printf("batching saves CPU time, but the first packet of a batch waits for the last: latency against throughput\n");

    /* correctness: both ways send the same bytes, and the batch rings 8 times less */
    rc = (bytes_a == bytes_b && a.doorbell.writes == 32 && b.doorbell.writes == 4) ? 0 : 1;
done:   /* C has no destructor: one cleanup label frees every ring */
    free(one.desc);
    free(a.desc);
    free(b.desc);
    return rc;
}
