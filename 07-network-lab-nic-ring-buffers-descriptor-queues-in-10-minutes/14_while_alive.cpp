// NIC Ring Buffers and Descriptor Queues Explained - slide 14: thank you
// Build: make 14_while_alive
#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

// the imaginary tomjnet.h of the slide
struct Topic { std::string name; };
static std::size_t episodes = 0;
static const char* const plan[] = {
    "Polling vs Interrupts: Why Low Latency Systems Poll",
    "Kernel Bypass: DPDK, AF_XDP and Low Latency Networking",
    "Inside the CPU: What We Learned About Networking and NICs"};
static bool alive() { return episodes < 3; }                      // three episodes left in this lab, then the demo ends
static Topic next_network_topic() { return Topic{plan[episodes++]}; }
static Topic viewer_request() { return Topic{"viewer request #" + std::to_string(episodes)}; }
static void subscribe() { std::cout << "  subscribed to TomJNet\n"; }

static std::size_t watched = 0;
static void watch(const std::optional<Topic>& t) {
    if (t) { ++watched; std::cout << "  watched: " << t->name << "\n"; }
}

// the ring of the video in its smallest form: power of two capacity, push drops when full
template <class T, std::size_t N>
class Ring {
    static_assert(N > 0 && (N & (N - 1)) == 0, "capacity must be a power of two");
    std::array<T, N> buf_{};
    std::size_t head_ = 0;               // free running, masked on access
    std::size_t tail_ = 0;
    std::size_t missed_ = 0;
public:
    bool push(T value) {
        if (tail_ - head_ == N) { ++missed_; return false; }
        buf_[tail_++ & (N - 1)] = std::move(value);
        return true;
    }
    std::optional<T> pop() {
        if (head_ == tail_) return std::nullopt;
        return std::move(buf_[head_++ & (N - 1)]);
    }
    std::size_t size() const { return tail_ - head_; }
    std::size_t missed() const { return missed_; }
};

int main() {
    Ring<Topic, 8> lab;                  // power of two, no heap
    while (alive()) {
        lab.push(next_network_topic());  // polling vs interrupts
        lab.push(viewer_request());      // dropped only when full
        watch(lab.pop());                // clean and refill
        subscribe();                     // lifetime benefit
    }

    std::cout << "pushed " << 2 * episodes << ", watched " << watched << ", still in the ring " << lab.size()
              << ", missed " << lab.missed() << "\n";
    while (lab.size() > 0) watch(lab.pop());          // drain the ring before exit
    return lab.missed() == 0 && watched == 2 * episodes ? 0 : 1;
}
