// Inside the Linux Network Stack: From Socket to NIC - slide 9: a latency budget in c++
// Build: make 09_latency_budget
//
// A model with typical orders of magnitude, not a measurement: change the numbers and reason about the path.
#include <cstddef>
#include <iomanip>
#include <iostream>

int main() {
    struct Stage { const char* name; double ns; int copies; int switches; };
    const Stage rx_path[] = {               // typical orders of magnitude
        {"hard IRQ, schedule NAPI",   2000, 0, 0},
        {"softirq: NAPI poll, driver", 3000, 0, 0},
        {"IP and TCP or UDP",          2000, 0, 0},
        {"socket buffer, wake up",     5000, 0, 1},  // context switch
        {"recv: system call + copy",   1000, 1, 0},  // kernel to user
    };
    double total = 0;
    for (const Stage& s : rx_path) total += s.ns;    // about 13 us
    // the wire time of a short frame at 10 Gb/s: under 0.1 us

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "receive path, typical costs (a model, not a measurement)\n";
    std::cout << "  " << std::left << std::setw(30) << "stage" << std::right << std::setw(9) << "us" << std::setw(9) << "share"
              << std::setw(8) << "copies" << std::setw(10) << "switches" << "\n";
    int copies = 0;
    int switches = 0;
    for (const Stage& s : rx_path) {
        std::cout << "  " << std::left << std::setw(30) << s.name << std::right << std::setw(9) << s.ns / 1000.0
                  << std::setw(8) << 100.0 * s.ns / total << "%" << std::setw(8) << s.copies << std::setw(10) << s.switches << "\n";
        copies += s.copies;
        switches += s.switches;
    }
    std::cout << "  " << std::left << std::setw(30) << "total" << std::right << std::setw(9) << total / 1000.0
              << std::setw(8) << 100.0 << "%" << std::setw(8) << copies << std::setw(10) << switches << "\n\n";

    // what if: the questions of the slide, answered by the same table
    const std::size_t irq = 0;
    const std::size_t wake = 3;
    const double no_wake = total - rx_path[wake].ns;                 // a thread already spins on the core
    const double no_wake_no_irq = no_wake - rx_path[irq].ns;         // and it polls, so no interrupt either
    std::cout << "what if a thread is already spinning (no wake up): " << no_wake / 1000.0 << " us, "
              << 100.0 * (total - no_wake) / total << "% saved\n";
    std::cout << "what if it also polls the NIC (no interrupt):      " << no_wake_no_irq / 1000.0 << " us, "
              << 100.0 * (total - no_wake_no_irq) / total << "% saved\n";

    // the wire: 64 byte frame + 8 bytes of preamble + 12 bytes of gap = 84 bytes = 672 bits
    const double wire_ns = 84.0 * 8.0 / 10e9 * 1e9;
    std::cout << "wire time of a short frame at 10 Gb/s: " << wire_ns << " ns, " << std::setprecision(0) << total / wire_ns
              << " times less than the receive path of the host\n";
    return 0;
}
