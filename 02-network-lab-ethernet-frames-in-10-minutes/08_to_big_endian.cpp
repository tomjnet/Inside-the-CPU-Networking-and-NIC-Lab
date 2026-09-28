// Ethernet Frames in 10 Minutes: MAC Addresses, Headers and Payloads - slide 8: network byte order
// Build: make 08_to_big_endian
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(__linux__)
#include <arpa/inet.h>
#endif

// Host to network order for 16 bits: what htons does. No copies
constexpr std::uint16_t to_big_endian_16(std::uint16_t x) {
    if constexpr (std::endian::native == std::endian::big)
        return x;                               // already wire order
    else
        return static_cast<std::uint16_t>((x << 8) | (x >> 8));
}
// on x86 (little endian): 0x0800 is stored as 00 08, sent as 08 00
static_assert(to_big_endian_16(to_big_endian_16(0x0800)) == 0x0800);

// the 32 bit sibling: what htonl does (an IPv4 address, a sequence number)
constexpr std::uint32_t to_big_endian_32(std::uint32_t x) {
    if constexpr (std::endian::native == std::endian::big)
        return x;
    else
        return ((x & 0x000000ffu) << 24) | ((x & 0x0000ff00u) << 8)
             | ((x & 0x00ff0000u) >> 8) | ((x & 0xff000000u) >> 24);
}

// the fully portable way: no question about the host at all, write the high byte first
static void put_be16(unsigned char* out, std::uint16_t x) {
    out[0] = static_cast<unsigned char>(x >> 8);
    out[1] = static_cast<unsigned char>(x & 0xff);
}

template <class T>
static void show_bytes(const char* what, T value) {
    unsigned char b[sizeof(T)];
    std::memcpy(b, &value, sizeof(T));
    std::printf("  %-34s", what);
    for (unsigned char c : b) std::printf(" %02x", static_cast<unsigned>(c));
    std::printf("\n");
}

int main() {
    const bool little = std::endian::native == std::endian::little;
    std::printf("this machine is %s endian\n\n", little ? "little" : "big");

    const std::uint16_t ethertype = 0x0800;                 // IPv4
    const std::uint32_t ip = 0xC0A80114u;                   // 192.168.1.20
    std::printf("bytes in memory, lowest address first (the order they would leave on the wire):\n");
    show_bytes("uint16_t 0x0800 as stored", ethertype);
    show_bytes("to_big_endian_16(0x0800)", to_big_endian_16(ethertype));
    show_bytes("uint32_t 0xC0A80114 as stored", ip);
    show_bytes("to_big_endian_32(0xC0A80114)", to_big_endian_32(ip));

    unsigned char wire[2];
    put_be16(wire, ethertype);
    std::printf("  %-34s %02x %02x\n", "put_be16: byte writes, any host",
                static_cast<unsigned>(wire[0]), static_cast<unsigned>(wire[1]));

    // correctness: the converted value must sit in memory as 08 00, and the swap must be its own inverse
    const std::uint16_t net = to_big_endian_16(ethertype);
    unsigned char mem[2];
    std::memcpy(mem, &net, 2);
    bool ok = mem[0] == 0x08 && mem[1] == 0x00 && wire[0] == 0x08 && wire[1] == 0x00;
    ok = ok && to_big_endian_16(net) == ethertype && to_big_endian_32(to_big_endian_32(ip)) == ip;

#if defined(__linux__)
    // the sockets API does the same job: htons = host to network short, htonl = host to network long
    const bool same = htons(ethertype) == net && htonl(ip) == to_big_endian_32(ip) && ntohs(net) == ethertype;
    std::printf("\nhtons(0x0800) == to_big_endian_16(0x0800), and htonl agrees too: %s\n", same ? "yes" : "NO");
    ok = ok && same;
#else
    std::printf("\nthis sample needs Linux: one more line would compare htons and htonl with our functions\n");
#endif

    std::printf("wire order is 08 00 and the swap is its own inverse: %s\n", ok ? "checks passed" : "CHECK FAILED");
    return ok ? 0 : 1;
}
