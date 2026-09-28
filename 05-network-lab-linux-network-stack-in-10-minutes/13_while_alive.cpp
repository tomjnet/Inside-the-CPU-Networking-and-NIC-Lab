// Inside the Linux Network Stack: From Socket to NIC - slide 13: thank you
// Build: make 13_while_alive
#include <cstddef>
#include <iostream>
#include <queue>
#include <string>

// the imaginary tomjnet.h of the slide
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_network_topic() {
    static const char* topics[] = {"How a NIC Works: RX, TX, DMA and Interrupts",
                                   "NIC Ring Buffers and Descriptor Queues Explained",
                                   "Polling vs Interrupts: Why Low Latency Systems Poll"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    std::queue<Topic> todo;
    while (alive()) {
        todo.push(next_network_topic()); // next: how a NIC works
        todo.push(viewer_request());     // RX, TX, DMA, interrupts
        subscribe();                     // zero copies, one click
    }

    std::cout << "queued topics: " << todo.size() << "\n";
    while (!todo.empty()) {
        std::cout << "  " << todo.front().name << "\n";
        todo.pop();
    }
    return 0;
}
