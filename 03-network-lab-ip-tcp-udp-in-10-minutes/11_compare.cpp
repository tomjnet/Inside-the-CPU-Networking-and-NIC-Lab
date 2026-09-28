// IP, TCP and UDP: What Actually Happens to a Packet - slide 11: header sizes and guarantees
// Build: make 11_compare
#include <cstdio>
#include <iostream>

int main() {
    // header bytes on the wire for one 100 byte message
    struct Proto { const char* name; int header; const char* gives; };
    const Proto table[] = {
        {"Ethernet", 14 + 4, "one hop, MAC to MAC, FCS"},
        {"IPv4",     20,     "host to host, best effort, TTL"},
        {"UDP",      8,      "ports, datagrams, no guarantees"},
        {"TCP",      20,     "ports, ordered reliable byte stream"},
    };
    int udp_total = 18 + 20 + 8 + 100;    // 146 bytes, 46 of overhead
    int tcp_total = 18 + 20 + 20 + 100;   // 158 bytes, 58 of overhead

    std::printf("%-10s %7s   %s\n", "layer", "header", "what it gives you");
    for (const Proto& p : table) std::printf("%-10s %5d B   %s\n", p.name, p.header, p.gives);

    std::printf("\n100 byte message: %d bytes on the wire with UDP, %d with TCP\n", udp_total, tcp_total);

    const int eth = table[0].header, ip = table[1].header, udp = table[2].header, tcp = table[3].header;
    std::printf("\n%-9s %12s %12s %14s %14s\n", "message", "UDP on wire", "TCP on wire", "UDP overhead", "TCP overhead");
    const int sizes[] = {10, 100, 1000, 1460};
    for (int n : sizes) {
        const int u = eth + ip + udp + n;
        const int t = eth + ip + tcp + n;
        std::printf("%5d B   %10d B %10d B %13.1f%% %13.1f%%\n", n, u, t, 100.0 * (u - n) / u, 100.0 * (t - n) / t);
    }
    std::cout << "(the Ethernet payload has a 46 byte minimum: the 10 byte UDP message is padded on a real link)\n";

    std::cout << "\nwhat each transport makes you wait for:\n";
    std::cout << "  UDP: nothing. No handshake, no ACK, no window: loss and order are the application's job\n";
    std::cout << "  TCP: the handshake (1 round trip), ACKs, the two windows, retransmissions, in order delivery\n";
    std::cout << "  TCP options (timestamps, SACK) often add 12 bytes or more: the header is 20 to 60 bytes\n";

    const bool ok = udp_total == eth + ip + udp + 100 && tcp_total == eth + ip + tcp + 100;
    return ok ? 0 : 1;
}
