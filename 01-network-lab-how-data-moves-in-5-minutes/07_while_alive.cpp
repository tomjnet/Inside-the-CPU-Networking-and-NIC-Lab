// Computer Networking in 5 Minutes: How Data Moves Between Machines - slide 7: thank you
// Build: make 07_while_alive
#include <iostream>
#include <queue>
#include <string>

// #include "tomjnet.h"  (imaginary on the slide: these are its contents)
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_network_topic() {
    static const char* topics[] = {"Ethernet frames: MAC addresses, headers and payloads",
                                   "IP, TCP and UDP: what actually happens to a packet",
                                   "Sockets in C++: your first network program"};
    return Topic{topics[episodes++]};
}
static void send(const Topic& t) { std::cout << "next: " << t.name << " (100 B + 46 B headers)\n"; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    std::queue<Topic> network_lab;
    while (alive()) {
        network_lab.push(next_network_topic());  // Ethernet frames
        send(network_lab.front());               // 100 B + 46 B headers
        network_lab.pop();                       // first in, first out
        subscribe();                             // lifetime benefit
    }
    std::cout << "queue empty: " << std::boolalpha << network_lab.empty() << "\n";
    return 0;
}
