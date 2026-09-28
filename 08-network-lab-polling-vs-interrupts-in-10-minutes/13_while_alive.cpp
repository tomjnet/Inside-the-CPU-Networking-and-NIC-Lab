// Polling vs Interrupts: Why Low Latency Systems Poll - slide 13: thank you
// Build: make 13_while_alive
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// The imaginary tomjnet.h of the slide.
struct Topic { std::string name; };
template <class T>
struct Ring {
    std::vector<T> items;
    void push(T t) { items.push_back(std::move(t)); }
};
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_network_topic() {
    static const char* topics[] = {"Kernel Bypass: DPDK, AF_XDP and Low Latency Networking",
                                   "What We Learned About Networking and NICs",
                                   "Low Latency C++ Lab"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

// #include "tomjnet.h"   (imaginary: the block above stands in for it)

int main() {
    Ring<Topic> rx;                       // no interrupt needed
    while (alive()) {
        rx.push(next_network_topic());    // next: kernel bypass
        rx.push(viewer_request());        // polled every episode
        subscribe();                      // lifetime benefit
    }

    std::cout << "polled from the ring:\n";
    for (const Topic& t : rx.items) std::cout << "  " << t.name << "\n";
    return 0;
}
