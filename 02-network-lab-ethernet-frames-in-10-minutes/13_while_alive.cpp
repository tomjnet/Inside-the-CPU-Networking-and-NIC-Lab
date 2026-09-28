// Ethernet Frames in 10 Minutes: MAC Addresses, Headers and Payloads - slide 13: thank you
// Build: make 13_while_alive
#include <iostream>
#include <queue>
#include <string>

// the imaginary tomjnet.h of the slide
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_network_topic() {
    static const char* topics[] = {"IP, TCP and UDP: what actually happens to a packet",
                                   "Sockets in C++: your first network program",
                                   "Inside the Linux network stack: from socket to NIC"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    std::queue<Topic> wire;
    while (alive()) {
        wire.push(next_network_topic());   // IP, TCP and UDP
        wire.push(viewer_request());       // payload of the frame
        subscribe();                       // lifetime benefit
    }

    std::cout << "frames on the wire, first in first out:\n";
    while (!wire.empty()) {
        std::cout << "  " << wire.front().name << "\n";
        wire.pop();
    }
    return 0;
}
