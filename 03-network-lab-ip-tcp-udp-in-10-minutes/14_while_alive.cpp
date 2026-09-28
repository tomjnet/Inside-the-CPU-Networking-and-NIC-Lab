// IP, TCP and UDP: What Actually Happens to a Packet - slide 14: thank you
// Build: make 14_while_alive
#include <cstddef>
#include <iostream>
#include <queue>
#include <string>

// the imaginary tomjnet.h of the slide
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_network_topic() {
    static const char* topics[] = {"Sockets in C++: Your First Network Program",
                                   "Inside the Linux Network Stack: From Socket to NIC",
                                   "How a NIC Works: RX, TX, DMA and Interrupts"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    std::queue<Topic> todo;
    while (alive()) {
        todo.push(next_network_topic());  // next: sockets in C++
        todo.push(viewer_request());      // FIFO, like a socket buffer
        subscribe();                      // no retransmission needed
    }

    std::cout << "queued topics, first in first out:\n";
    const std::size_t queued = todo.size();
    while (!todo.empty()) {
        std::cout << "  " << todo.front().name << "\n";
        todo.pop();
    }
    return queued == 6 ? 0 : 1;
}
