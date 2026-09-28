// Inside the CPU: What We Learned About Networking and NICs - slide 5: thank you
// Build: make 05_while_alive
#include <cstddef>
#include <iostream>
#include <queue>
#include <string>

// the imaginary tomjnet.h of the slide
struct Lab { std::string name; };
static int episodes = 0;
static bool alive() { return episodes < 2; }                  // two labs, then the demo ends
static Lab next_lab() {
    static const char* labs[] = {"Inside the CPU: Low Latency C++ Lab", "Inside the CPU: Cache and NUMA Lab"};
    return Lab{labs[episodes++]};
}
static void count_copies(const Lab& lab) { std::cout << "next lab: " << lab.name << " (passed by reference: 0 copies)\n"; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

int main() {
    std::queue<Lab> next_labs;           // FIFO, like an RX ring
    while (alive()) {
        next_labs.push(next_lab());      // Low Latency, Cache and NUMA
        count_copies(next_labs.back());  // and count the wake ups
        subscribe();                     // lifetime benefit
    }
    const std::size_t queued = next_labs.size();
    std::cout << queued << " labs queued, first out: " << next_labs.front().name << "\n";
    return queued == 2 ? 0 : 1;
}
