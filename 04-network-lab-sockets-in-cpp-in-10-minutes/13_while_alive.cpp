// Sockets in C++: Your First Network Program - slide 13: thank you
// Build: make 13_while_alive
#include <iostream>
#include <string>

// the imaginary "tomjnet.h" of the slide
struct Topic { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 3; }                 // three episodes, then the demo ends
static Topic next_network_topic() {
    static const char* topics[] = {"Inside the Linux Network Stack: From Socket to NIC",
                                   "How a NIC Works: RX, TX, DMA and Interrupts",
                                   "NIC Ring Buffers and Descriptor Queues Explained"};
    return Topic{topics[episodes++]};
}
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

struct Channel {
    std::string peer;
    int sent = 0;
    void send(const Topic& t) { std::cout << "  send to " << peer << ": " << t.name << "\n"; ++sent; }
};
static Channel connect_to(const std::string& name) {
    std::cout << "connected to " << name << "\n";
    return Channel{name};
}

int main() {
    Channel viewers = connect_to("TomJNet");    // one handshake
    while (alive()) {
        viewers.send(next_network_topic());     // the network stack
        viewers.send(viewer_request());         // blocking, worth it
        subscribe();                            // lifetime benefit
    }

    std::cout << viewers.sent << " topics sent over one connection\n";
    return 0;
}
