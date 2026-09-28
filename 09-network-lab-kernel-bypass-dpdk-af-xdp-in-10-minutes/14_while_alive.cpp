// Kernel Bypass: DPDK, AF_XDP and Low Latency Networking - slide 14: thank you
// Build: make 14_while_alive
#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// ---- the imaginary "tomjnet.h" of the slide ----
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_network_topic() {
    static const char* topics[] = {"Inside the CPU: What We Learned About Networking and NICs",
                                   "the next lab of the channel",
                                   "a low latency feed handler, end to end"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

template <class T>
struct RxRing {                                              // a ring of topics: push at head, no system call
    std::vector<T> slot = std::vector<T>(8);
    std::size_t head = 0;
    void push(T t) {
        std::cout << "  ring slot " << (head & 7) << ": " << t.name << "\n";
        slot[head++ & 7] = std::move(t);
    }
};
// ---- end of tomjnet.h ----

int main() {
    RxRing<Topic> ring;
    while (alive()) {
        ring.push(next_network_topic());   // the series summary
        ring.push(viewer_request());       // no system call needed
        subscribe();                       // zero copy benefit
    }

    std::cout << ring.head << " topics in the ring, next episode: the series summary\n";
    return 0;
}
