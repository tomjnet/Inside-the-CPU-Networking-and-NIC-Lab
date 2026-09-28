/* NIC Ring Buffers and Descriptor Queues Explained - slide 6: the ring as a c++ model (C version of 06_ring_model.cpp) */
/* Build: make 06_ring_model_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {                 /* slide 3 */
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t vlan;
} RxDescriptor;
_Static_assert(sizeof(RxDescriptor) == 16, "a descriptor is 16 bytes");

typedef struct {
    RxDescriptor *desc;          /* shared by NIC and driver */
    uint32_t size;
    uint32_t mask;               /* size - 1, size a power of two */
    uint32_t head;               /* NIC: next descriptor to fill */
    uint32_t tail;               /* driver: end of posted buffers */
    uint32_t clean;              /* driver: next packet to read */
    uint64_t rx_missed;          /* arrivals with no free buffer */
} RxRing;
/* free running counters: desc[head & mask] does the wrap */
/* full: head == tail, every posted buffer holds a packet */

/* C has no constructor or destructor: ring_init allocates, ring_free must be called on every path */
static bool ring_init(RxRing *r, uint32_t size) {
    r->desc = calloc(size, sizeof(RxDescriptor));
    r->size = size;
    r->mask = size - 1;
    r->head = 0;
    r->tail = size;
    r->clean = 0;
    r->rx_missed = 0;
    return r->desc != NULL;
}
static void ring_free(RxRing *r) { free(r->desc); r->desc = NULL; }

static bool is_power_of_two(uint32_t n) { return n != 0 && (n & (n - 1)) == 0; }

static void print_state(const char *when, const RxRing *r) {
    printf("  %s: head=%u tail=%u clean=%u | free buffers (tail - head)=%u | packets waiting (head - clean)=%u%s\n",
           when, (unsigned)r->head, (unsigned)r->tail, (unsigned)r->clean,
           (unsigned)(r->tail - r->head), (unsigned)(r->head - r->clean),
           r->head == r->tail ? " | FULL" : "");
}

int main(void) {
    const uint32_t size = 8;
    if (!is_power_of_two(size)) return 1;          /* the mask only works for a power of two */
    RxRing ring, old;
    if (!ring_init(&ring, size)) return 1;
    if (!ring_init(&old, size)) { ring_free(&ring); return 1; }
    printf("ring of %u descriptors, %zu bytes, mask=%u\n",
           (unsigned)ring.size, ring.size * sizeof(RxDescriptor), (unsigned)ring.mask);
    print_state("start        ", &ring);

    printf("the wrap, counter & mask:");
    for (uint32_t c = 5; c < 12; ++c) printf(" %u->%u", (unsigned)c, (unsigned)(c & ring.mask));
    printf("\n");

    ring.head += 5;                                /* the NIC filled 5 descriptors */
    print_state("NIC filled 5 ", &ring);
    ring.clean += 3; ring.tail += 3;               /* the driver read 3 packets and posted the buffers again */
    print_state("driver did 3 ", &ring);
    ring.head += 6;                                /* 6 more arrivals: the last free buffer is used */
    print_state("NIC filled 6 ", &ring);
    bool full_seen = ring.head == ring.tail;

    /* free running counters survive the 32 bit overflow: unsigned subtraction is modulo 2^32 */
    old.head = 0xFFFFFFFCu; old.clean = old.head; old.tail = old.head + size;   /* tail has already wrapped to 4 */
    printf("near the 32 bit overflow:\n");
    print_state("before       ", &old);
    old.head += 6;                                 /* head wraps past zero too */
    print_state("6 arrivals   ", &old);
    bool overflow_ok = (old.tail - old.head) == 2 && (old.head - old.clean) == 6;

    printf("real hardware keeps wrapped indexes in its head and tail registers and leaves one slot empty;\n"
           "the model keeps free running counters, so all %u descriptors are usable\n", (unsigned)size);
    ring_free(&old);
    ring_free(&ring);
    return full_seen && overflow_ok ? 0 : 1;
}
