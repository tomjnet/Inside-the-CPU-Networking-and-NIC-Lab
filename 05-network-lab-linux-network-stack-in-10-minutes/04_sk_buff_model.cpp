// Inside the Linux Network Stack: From Socket to NIC - slide 4: sk_buff: headers without copies
// Build: make 04_sk_buff_model
#include <cstddef>
#include <cstring>
#include <iostream>

int main() {
    struct SkBuff {                  // model of the kernel's sk_buff
        unsigned char buf[256];      // one buffer, allocated once
        std::size_t data = 128;      // payload starts after the headroom
        std::size_t tail = 128;
        void put(std::size_t n)  { tail += n; }  // payload: the 1 copy
        void push(std::size_t n) { data -= n; }  // header: 0 copies
    };
    // skb.put(100);  skb.push(20);  skb.push(20);  skb.push(14);
    // TCP, IP, Ethernet: three headers, zero copies of the payload

    SkBuff skb{};
    std::size_t payload_bytes_copied = 0;
    std::size_t header_bytes_written = 0;

    auto show = [&skb](const char* step) {
        std::cout << "  " << step << "  data=" << skb.data << " tail=" << skb.tail
                  << " length=" << (skb.tail - skb.data) << " headroom left=" << skb.data << "\n";
    };

    std::cout << "one buffer of " << sizeof skb.buf << " bytes, 128 bytes of headroom reserved\n";
    show("empty              ");

    // the application's message: copied into the kernel buffer exactly once (copy 1 of the video)
    unsigned char message[100];
    for (std::size_t i = 0; i < sizeof message; ++i) message[i] = static_cast<unsigned char>('A' + i % 26);
    const unsigned char* payload_at = skb.buf + skb.tail;     // where the payload will live, for ever
    std::memcpy(skb.buf + skb.tail, message, sizeof message);
    skb.put(sizeof message);
    payload_bytes_copied += sizeof message;
    show("put(100)  payload  ");

    // each layer moves data back and writes its header in the headroom: the payload is not touched
    struct Layer { const char* step; std::size_t size; unsigned char fill; };
    const Layer layers[] = {
        {"push(20)  TCP      ", 20, 0x06},
        {"push(20)  IP       ", 20, 0x45},
        {"push(14)  Ethernet ", 14, 0xEE},
    };
    for (const Layer& l : layers) {
        skb.push(l.size);
        std::memset(skb.buf + skb.data, l.fill, l.size);
        header_bytes_written += l.size;
        show(l.step);
    }

    const bool payload_still_there = (payload_at == skb.buf + 128) && std::memcmp(payload_at, message, sizeof message) == 0;
    std::cout << "frame on the way to the driver: " << (skb.tail - skb.data) << " bytes = 14 + 20 + 20 + 100\n";
    std::cout << "payload bytes copied: " << payload_bytes_copied << " (once), header bytes written: " << header_bytes_written
              << ", payload moved: " << (payload_still_there ? "never" : "YES, the model is broken") << "\n";
    std::cout << "without headroom every header would shift the payload: 3 extra copies of 100 bytes\n";
    return payload_still_there && skb.tail - skb.data == 154 ? 0 : 1;
}
