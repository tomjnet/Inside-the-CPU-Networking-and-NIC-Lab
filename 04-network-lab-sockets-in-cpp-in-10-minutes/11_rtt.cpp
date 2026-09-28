// Sockets in C++: Your First Network Program - slide 11: round trip time over loopback: p50 and p99
// Build: make 11_rtt
#include <iostream>

#if defined(__linux__)
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <functional>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

sockaddr* sa(sockaddr_in& a) { return reinterpret_cast<sockaddr*>(&a); }

class Fd {                                  // owns one descriptor (slide 5)
    int fd_ = -1;
public:
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
    int get() const { return fd_; }
};

auto check(auto rc, const char* what) {     // -1 and errno become an exception (slide 6)
    if (rc < 0)
        throw std::system_error(errno, std::generic_category(), what);
    return rc;
}

void recv_all(int fd, char* p, std::size_t want) {      // the receive loop of slide 9
    while (want > 0) {
        auto n = check(recv(fd, p, want, 0), "recv");
        if (n == 0) throw std::runtime_error("peer closed");
        p += n; want -= n;
    }
}

// a blocking call that waits longer than this fails with EAGAIN: the demo can never hang
static void give_up_after(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv), "setsockopt");
}

static void no_delay(int fd) {                          // slide 10
    int one = 1;
    check(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one), "setsockopt");
}

using Clock = std::chrono::steady_clock;
static double ns_between(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration<double, std::nano>(t1 - t0).count();
}

// the echo server of slide 8 in its own thread: it sleeps in recv until the client sends
static void server(int lis, std::string& error_out) {
    try {
        Fd conn{check(accept(lis, nullptr, nullptr), "accept")};
        give_up_after(conn.get(), 5);
        no_delay(conn.get());
        char buf[4096];
        for (;;) {
            auto n = check(recv(conn.get(), buf, sizeof buf, 0), "recv");
            if (n == 0) break;
            check(send(conn.get(), buf, static_cast<std::size_t>(n), MSG_NOSIGNAL), "send");
        }
    } catch (const std::exception& e) {
        error_out = e.what();
    }
}
#endif

int main() {
#if defined(__linux__)
    try {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);                           // the kernel picks the port
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Fd lis{check(socket(AF_INET, SOCK_STREAM, 0), "socket")};
        check(bind(lis.get(), sa(addr), sizeof addr), "bind");
        check(listen(lis.get(), 16), "listen");
        socklen_t alen = sizeof addr;
        check(getsockname(lis.get(), sa(addr), &alen), "getsockname");
        give_up_after(lis.get(), 5);
        std::string server_error;
        std::thread t(server, lis.get(), std::ref(server_error));

        bool ok = false;
        try {
            Fd cli{check(socket(AF_INET, SOCK_STREAM, 0), "socket")};
            give_up_after(cli.get(), 5);
            check(connect(cli.get(), sa(addr), sizeof addr), "connect");
            no_delay(cli.get());

            char msg[64];
            char reply[64] = {};
            for (std::size_t i = 0; i < sizeof msg; ++i) msg[i] = static_cast<char>('a' + i % 26);
            for (int i = 0; i < 1000; ++i) {                // warm up: caches, branch predictor, both threads awake
                check(send(cli.get(), msg, 64, MSG_NOSIGNAL), "send");
                recv_all(cli.get(), reply, 64);
            }

            std::vector<double> rtt_ns;                 // one sample per ping
            rtt_ns.reserve(20000);                      // (added) no allocation inside the timed loop
            for (int i = 0; i < 20000; ++i) {
                auto t0 = std::chrono::steady_clock::now();
                check(send(cli.get(), msg, 64, MSG_NOSIGNAL), "send");
                recv_all(cli.get(), reply, 64);         // 4 system calls per trip
                auto t1 = std::chrono::steady_clock::now();
                rtt_ns.push_back(ns_between(t0, t1));
            }
            std::sort(rtt_ns.begin(), rtt_ns.end());    // p50 [n/2], p99 [n*99/100]

            const std::size_t n = rtt_ns.size();
            double sum = 0;
            for (double v : rtt_ns) sum += v;
            auto us = [](double ns) { return ns / 1000.0; };
            std::cout << n << " round trips of 64 bytes over 127.0.0.1, TCP_NODELAY on, this machine:\n";
            std::cout << "  min     " << us(rtt_ns.front()) << " us\n";
            std::cout << "  p50     " << us(rtt_ns[n / 2]) << " us\n";
            std::cout << "  p99     " << us(rtt_ns[n * 99 / 100]) << " us\n";
            std::cout << "  p99.9   " << us(rtt_ns[n * 999 / 1000]) << " us\n";
            std::cout << "  max     " << us(rtt_ns.back()) << " us\n";
            std::cout << "  average " << us(sum / static_cast<double>(n)) << " us  (hides the tail: report p50 and p99)\n";
            std::cout << "  p99 / p50 = " << rtt_ns[n * 99 / 100] / rtt_ns[n / 2] << "\n";
            std::cout << "no wire and no NIC: this is 4 system calls, 4 copies and 2 thread wake ups per trip.\n"
                         "Run it on real Linux hardware: a virtual machine or WSL adds its own jitter.\n";
            ok = std::equal(msg, msg + sizeof msg, reply);  // the last echo is byte for byte the message
        } catch (const std::exception& e) {
            std::cout << "client: " << e.what() << "\n";
        }                                                   // cli closes: the server's recv returns 0
        t.join();

        if (!server_error.empty()) std::cout << "server: " << server_error << "\n";
        return ok && server_error.empty() ? 0 : 1;
    } catch (const std::system_error& e) {
        std::cout << "setup failed: " << e.what() << "\n";
        return 1;
    }
#else
    std::cout << "this sample needs Linux: 20000 TCP round trips over 127.0.0.1 between two threads, "
                 "then p50, p99 and p99.9 of the round trip time\n";
    return 0;
#endif
}
