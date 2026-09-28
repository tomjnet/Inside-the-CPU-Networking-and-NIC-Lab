// Computer Networking in 5 Minutes: How Data Moves Between Machines - slide 4: where latency comes from
// Build: make 04_latency_sources
#include <iomanip>
#include <iostream>

// serialization: push every bit onto the wire, bits / link rate
double serialize_us(double bytes, double gbps) {
    return bytes * 8.0 / (gbps * 1e9) * 1e6;
}
// propagation: about 200000 km/s in fiber or copper, 5 us per km
double propagate_us(double km) { return km / 200000.0 * 1e6; }
// 146 B at 1 Gbps: 1.2 us; at 10 Gbps: 0.12 us
// 100 m of cable: 0.5 us; 1000 km of fiber: 5000 us, one way
// queueing and the two hosts' stacks: tens of us, and they vary

int main() {
    std::cout << std::fixed;

    std::cout << "serialization of one 146 B frame (100 B of data over UDP)\n";
    const double rates_gbps[] = {0.1, 1.0, 10.0, 25.0, 100.0};
    for (double gbps : rates_gbps) {
        std::cout << "  " << std::setw(6) << std::setprecision(1) << gbps << " Gbps: "
                  << std::setw(8) << std::setprecision(3) << serialize_us(146.0, gbps) << " us\n";
    }

    std::cout << "\npropagation, one way\n";
    const double distances_km[] = {0.001, 0.1, 1.0, 100.0, 1000.0};
    for (double km : distances_km) {
        std::cout << "  " << std::setw(9) << std::setprecision(3) << km << " km: "
                  << std::setw(10) << std::setprecision(3) << propagate_us(km) << " us\n";
    }

    // One way budget inside a data center. The first two lines are computed; the last two are
    // typical orders of magnitude that we assume here, not measurements: later episodes measure them.
    const double serialization = serialize_us(146.0, 10.0);
    const double propagation = propagate_us(0.1);
    const double queueing_typical = 5.0;          // one or two switch hops with light load
    const double host_stacks_typical = 2 * 15.0;  // sender plus receiver: system call, kernel, driver, NIC
    const double total = serialization + propagation + queueing_typical + host_stacks_typical;

    std::cout << "\none way budget: 146 B, 10 Gbps, 100 m of cable (typical values, assumed)\n" << std::setprecision(2);
    std::cout << "  serialization " << std::setw(7) << serialization << " us  (computed)\n";
    std::cout << "  propagation   " << std::setw(7) << propagation << " us  (computed)\n";
    std::cout << "  queueing      " << std::setw(7) << queueing_typical << " us  (typical, varies: the jitter)\n";
    std::cout << "  host stacks   " << std::setw(7) << host_stacks_typical << " us  (typical, both hosts)\n";
    std::cout << "  total         " << std::setw(7) << total << " us, of which the hosts are "
              << std::setprecision(0) << host_stacks_typical / total * 100.0 << " percent\n";
    std::cout << "the wire is fast; the hosts are the part this playlist learns to shrink\n";
    return 0;
}
