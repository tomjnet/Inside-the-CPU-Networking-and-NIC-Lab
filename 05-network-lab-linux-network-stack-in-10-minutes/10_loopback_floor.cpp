// Inside the Linux Network Stack: From Socket to NIC - slide 10: loopback round trip: the floor
// Build: make 10_loopback_floor
//
// One UDP socket sends a 64 byte datagram to itself over 127.0.0.1: two system calls, two copies and the
// protocol code, but no NIC, no hardware interrupt and no wake up. That is the floor of the kernel stack.
// Timings are printed for this machine and never asserted: read your own numbers on real Linux hardware.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

#if defined(__linux__)
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static const sockaddr* addr_of(const sockaddr_in& a) { return reinterpret_cast<const sockaddr*>(&a); }
#endif

static double now_ns() {
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void report(const char* what, std::vector<double>& ns) {
    std::sort(ns.begin(), ns.end());
    const std::size_t n = ns.size();
    std::cout << "  " << std::left << std::setw(44) << what << std::right << std::fixed << std::setprecision(1)
              << " min " << std::setw(7) << ns.front() << "  p50 " << std::setw(7) << ns[n / 2]
              << "  p99 " << std::setw(7) << ns[n * 99 / 100] << "  max " << std::setw(9) << ns.back() << "  (ns, this machine)\n";
}

int main() {
    // the part with no kernel in it, on every platform: two 64 byte copies. One sample is the average of a
    // batch of 1000 pairs, because a single pair is shorter than the resolution of the clock.
    static unsigned char src[64];
    static unsigned char mid[64];
    static unsigned char dst[64];
    unsigned char* volatile mid_p = mid;                     // volatile pointers: the copies cannot be deleted
    unsigned char* volatile dst_p = dst;
    std::memset(src, 0x5A, sizeof src);
    std::vector<double> copy_ns;
    copy_ns.reserve(2000);
    unsigned long long check = 0;
    for (int i = 0; i < 2000; ++i) {
        auto t0 = now_ns();
        for (int k = 0; k < 1000; ++k) {
            std::memcpy(mid_p, src, sizeof src);
            std::memcpy(dst_p, mid_p, sizeof mid);
        }
        copy_ns.push_back((now_ns() - t0) / 1000.0);
        check += dst[static_cast<std::size_t>(i) % sizeof dst];
    }
    std::cout << "round trip of one 64 byte message (checksum " << check << ")\n";
    report("user space: 2 copies, no kernel", copy_ns);

#if defined(__linux__)
    sockaddr_in me{};
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);             // 127.0.0.1
    me.sin_port = 0;                                         // the kernel picks an ephemeral port
    unsigned char msg[64];
    unsigned char buf[128];
    std::memset(msg, 0x5A, sizeof msg);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);       // UDP socket, loopback
    if (fd < 0) {
        std::cout << "socket: " << std::strerror(errno) << " (on Linux this prints p50 and p99 of the loopback round trip)\n";
        return 0;
    }
    timeval one_second{1, 0};                                // a lost datagram must not hang the sample
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &one_second, sizeof one_second);
    bind(fd, addr_of(me), sizeof me);              // port 0: ephemeral
    socklen_t len = sizeof me;
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&me), &len) != 0 || me.sin_port == 0) {
        std::cout << "bind or getsockname: " << std::strerror(errno) << " (no loopback here: nothing to measure)\n";
        close(fd);
        return 0;
    }
    std::cout << "UDP socket bound to 127.0.0.1:" << ntohs(me.sin_port) << ", sending to itself\n";

    for (int i = 0; i < 2000; ++i) {                         // warm up: caches, branch predictor, socket memory
        if (sendto(fd, msg, 64, 0, addr_of(me), sizeof me) != 64 || recv(fd, buf, sizeof buf, 0) != 64) {
            std::cout << "loopback round trip failed: " << std::strerror(errno) << " (nothing to measure)\n";
            close(fd);
            return 0;
        }
    }

    std::vector<double> rtt_ns;
    rtt_ns.reserve(20000);
    std::memset(buf, 0, sizeof buf);                         // so the check below proves a datagram arrived
    for (int i = 0; i < 20000; ++i) {
        auto t0 = now_ns();
        sendto(fd, msg, 64, 0, addr_of(me), sizeof me);  // syscall + copy
        recv(fd, buf, sizeof buf, 0);                    // syscall + copy
        rtt_ns.push_back(now_ns() - t0);   // no NIC, no IRQ, no wake up
    }
    std::sort(rtt_ns.begin(), rtt_ns.end());
    // p50 and p99: the floor of the stack, 2 syscalls and 2 copies
    close(fd);

    report("loopback UDP: 2 system calls, 2 copies", rtt_ns);
    const bool intact = std::memcmp(buf, msg, sizeof msg) == 0;
    std::cout << "last datagram: " << (intact ? "payload intact" : "MISMATCH") << "\n";
    std::cout << "the difference between the two rows is the kernel: system calls, UDP, IP, the loopback device\n";
    std::cout << "a real NIC adds the interrupt, the softirq and the wake up on top of this floor\n";
    return intact ? 0 : 1;
#else
    std::cout << "this sample needs Linux: it would show p50 and p99 of a UDP round trip over loopback,\n"
              << "typically a few microseconds: 2 system calls, 2 copies, no NIC, no interrupt, no wake up\n";
    return 0;
#endif
}
