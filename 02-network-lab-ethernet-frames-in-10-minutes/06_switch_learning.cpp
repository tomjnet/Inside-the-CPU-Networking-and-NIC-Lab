// Ethernet Frames in 10 Minutes: MAC Addresses, Headers and Payloads - slide 6: how a switch learns macs
// Build: make 06_switch_learning
#include <cstdint>
#include <cstdio>
#include <unordered_map>

static const int kPorts = 3;
static int floods = 0;
static int forwards = 0;
static int filtered = 0;

static void flood(int in_port) {               // every port except the one the frame came from
    ++floods;
    std::printf("    flood   :");
    for (int p = 1; p <= kPorts; ++p)
        if (p != in_port) std::printf(" port %d", p);
    std::printf("\n");
}

static void forward(int port) {
    ++forwards;
    std::printf("    forward : port %d only\n", port);
}

// A switch: learn the source, look up the destination. O(1) each
std::unordered_map<std::uint64_t, int> mac_table;   // MAC -> port

void on_frame(int in_port, std::uint64_t src, std::uint64_t dst) {
    mac_table[src] = in_port;                   // learn: src is here
    auto it = mac_table.find(dst);
    if (it == mac_table.end()) flood(in_port);  // unknown: all ports
    else if (it->second != in_port) forward(it->second);  // one port
    else ++filtered;                            // same port: the switch drops it (not on the slide)
}

static void send(const char* what, int in_port, std::uint64_t src, std::uint64_t dst) {
    std::printf("  %s (in on port %d)\n", what, in_port);
    on_frame(in_port, src, dst);
    std::printf("    table   : %u entries\n", static_cast<unsigned>(mac_table.size()));
}

int main() {
    const std::uint64_t A = 0x02000000aa01ULL;  // 48 bits in a 64 bit key
    const std::uint64_t B = 0x02000000bb02ULL;
    const std::uint64_t C = 0x02000000cc03ULL;
    const std::uint64_t BROADCAST = 0xffffffffffffULL;

    std::printf("three hosts: A on port 1, B on port 2, C on port 3, empty MAC table\n\n");
    send("1. A sends to B: B is unknown", 1, A, B);
    send("2. B replies to A: A was learned in step 1", 2, B, A);
    send("3. A sends to B again: C sees nothing", 1, A, B);
    send("4. C sends an ARP request to broadcast: never a source, so never in the table", 3, C, BROADCAST);
    send("5. B answers C with a unicast frame", 2, B, C);

    std::printf("\nfloods %d, forwards %d, filtered %d\n", floods, forwards, filtered);
    const bool ok = floods == 2 && forwards == 3 && filtered == 0 && mac_table.size() == 3;
    std::printf("expected 2 floods (one unknown, one broadcast) and 3 forwards: %s\n",
                ok ? "checks passed" : "CHECK FAILED");
    return ok ? 0 : 1;
}
