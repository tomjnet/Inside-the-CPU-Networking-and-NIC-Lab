/* Inside the Linux Network Stack: From Socket to NIC - slide 4: sk_buff: headers without copies (C version of 04_sk_buff_model.cpp) */
/* Build: make 04_sk_buff_model_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {                     /* model of the kernel's sk_buff */
    unsigned char buf[256];          /* one buffer, allocated once */
    size_t data;                     /* payload starts after the headroom */
    size_t tail;
} SkBuff;

/* C has no member functions: put and push take the buffer as a pointer, like the kernel's skb_put and skb_push */
static void skb_put(SkBuff *skb, size_t n)  { skb->tail += n; }  /* payload: the 1 copy */
static void skb_push(SkBuff *skb, size_t n) { skb->data -= n; }  /* header: 0 copies */
/* skb_put(&skb, 100);  skb_push(&skb, 20);  skb_push(&skb, 20);  skb_push(&skb, 14); */
/* TCP, IP, Ethernet: three headers, zero copies of the payload */

static void show(const SkBuff *skb, const char *step) {
    printf("  %s  data=%zu tail=%zu length=%zu headroom left=%zu\n",
           step, skb->data, skb->tail, skb->tail - skb->data, skb->data);
}

typedef struct { const char *step; size_t size; unsigned char fill; } Layer;

int main(void) {
    static SkBuff skb;
    skb.data = 128;
    skb.tail = 128;
    size_t payload_bytes_copied = 0;
    size_t header_bytes_written = 0;

    printf("one buffer of %zu bytes, 128 bytes of headroom reserved\n", sizeof skb.buf);
    show(&skb, "empty              ");

    /* the application's message: copied into the kernel buffer exactly once (copy 1 of the video) */
    unsigned char message[100];
    for (size_t i = 0; i < sizeof message; ++i) message[i] = (unsigned char)('A' + i % 26);
    const unsigned char *payload_at = skb.buf + skb.tail;     /* where the payload will live, for ever */
    memcpy(skb.buf + skb.tail, message, sizeof message);
    skb_put(&skb, sizeof message);
    payload_bytes_copied += sizeof message;
    show(&skb, "put(100)  payload  ");

    /* each layer moves data back and writes its header in the headroom: the payload is not touched */
    const Layer layers[] = {
        {"push(20)  TCP      ", 20, 0x06},
        {"push(20)  IP       ", 20, 0x45},
        {"push(14)  Ethernet ", 14, 0xEE},
    };
    for (size_t i = 0; i < sizeof layers / sizeof layers[0]; ++i) {
        const Layer *l = &layers[i];
        skb_push(&skb, l->size);
        memset(skb.buf + skb.data, l->fill, l->size);
        header_bytes_written += l->size;
        show(&skb, l->step);
    }

    const int payload_still_there = (payload_at == skb.buf + 128) && memcmp(payload_at, message, sizeof message) == 0;
    printf("frame on the way to the driver: %zu bytes = 14 + 20 + 20 + 100\n", skb.tail - skb.data);
    printf("payload bytes copied: %zu (once), header bytes written: %zu, payload moved: %s\n",
           payload_bytes_copied, header_bytes_written, payload_still_there ? "never" : "YES, the model is broken");
    printf("without headroom every header would shift the payload: 3 extra copies of 100 bytes\n");
    return payload_still_there && skb.tail - skb.data == 154 ? 0 : 1;
}
