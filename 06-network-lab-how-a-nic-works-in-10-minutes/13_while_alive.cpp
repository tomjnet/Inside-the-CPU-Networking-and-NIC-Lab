// How a NIC Works: RX, TX, DMA and Interrupts - slide 13: thank you
// Build: make 13_while_alive
#include <iostream>
#include <queue>
#include <string>

// The imaginary tomjnet.h of the slide, written out so the program builds and ends.
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_network_topic() {
    static const char* topics[] = {"NIC ring buffers and descriptor queues",
                                   "Polling vs interrupts: why low latency systems poll",
                                   "Kernel bypass: DPDK, AF_XDP and low latency networking"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    std::queue<Topic> rx_ring;           // next: descriptor rings
    while (alive()) {
        rx_ring.push(next_network_topic());   // NIC ring buffers
        rx_ring.push(viewer_request());       // no drops here
        subscribe();                          // lifetime benefit
    }

    std::cout << "queued topics, first in first out:\n";
    while (!rx_ring.empty()) {
        std::cout << "  " << rx_ring.front().name << "\n";
        rx_ring.pop();
    }
    return 0;
}
