// Sockets in C++: Your First Network Program - slide 10: tcp_nodelay: switching nagle off
// Build: make 10_nodelay
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

constexpr int kRounds = 25;                 // per mode; a Nagle stall is about 40 ms, so at most about 1 s
constexpr std::size_t kHalf = 32;           // the request is written in two halves: write, write, read

// the server answers only when the whole 64 byte request is in: it has nothing to send before that,
// so its ACK for the first half is a delayed ACK, and Nagle on the client waits for exactly that ACK
static void server(int lis, std::string& error_out) {
    try {
        Fd conn{check(accept(lis, nullptr, nullptr), "accept")};
        give_up_after(conn.get(), 5);
        char request[2 * kHalf];
        for (int i = 0; i < 2 * kRounds; ++i) {
            recv_all(conn.get(), request, sizeof request);
            check(send(conn.get(), request, 8, MSG_NOSIGNAL), "send");
        }
    } catch (const std::exception& e) {
        error_out = e.what();
    }
}

struct Stats { double median_ms; double max_ms; };

// write, write, read: the pattern that Nagle punishes
static Stats exchanges(int fd) {
    std::vector<double> ms;
    char half[kHalf] = {};
    char reply[8];
    for (int i = 0; i < kRounds; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        check(send(fd, half, sizeof half, MSG_NOSIGNAL), "send");      // leaves at once: nothing is in flight
        check(send(fd, half, sizeof half, MSG_NOSIGNAL), "send");      // Nagle: waits for the ACK of the first
        recv_all(fd, reply, sizeof reply);
        auto t1 = std::chrono::steady_clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(ms.begin(), ms.end());
    return Stats{ms[ms.size() / 2], ms.back()};
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

            int before = -1;
            socklen_t blen = sizeof before;
            check(getsockopt(cli.get(), IPPROTO_TCP, TCP_NODELAY, &before, &blen), "getsockopt");
            std::cout << "TCP_NODELAY on a new socket: " << before << " (Nagle is on by default)\n";
            const Stats nagle = exchanges(cli.get());

            // Nagle: a small send waits for the ACK of the previous one
            // TCP_NODELAY: every send leaves at once, one packet per send
            int one = 1;
            check(setsockopt(cli.get(), IPPROTO_TCP, TCP_NODELAY,
                             &one, sizeof one), "setsockopt");

            int on = 0;
            socklen_t len = sizeof on;
            getsockopt(cli.get(), IPPROTO_TCP, TCP_NODELAY, &on, &len);

            std::cout << "TCP_NODELAY after setsockopt: " << on << "\n";
            const Stats nodelay = exchanges(cli.get());

            std::cout << "write 32 bytes, write 32 bytes, read the reply, " << kRounds << " times, this machine:\n";
            std::cout << "  Nagle on:     median " << nagle.median_ms << " ms, worst " << nagle.max_ms << " ms\n";
            std::cout << "  TCP_NODELAY:  median " << nodelay.median_ms << " ms, worst " << nodelay.max_ms << " ms\n";
            std::cout << "  a worst case near 40 ms with Nagle on is the delayed ACK of the other side;\n"
                         "  the first exchanges of a connection are often fast, the stall shows up later\n";
            ok = before == 0 && on != 0;
        } catch (const std::exception& e) {
            std::cout << "client: " << e.what() << "\n";
        }
        t.join();

        if (!server_error.empty()) std::cout << "server: " << server_error << "\n";
        return ok && server_error.empty() ? 0 : 1;
    } catch (const std::system_error& e) {
        std::cout << "setup failed: " << e.what() << "\n";
        return 1;
    }
#else
    std::cout << "this sample needs Linux: it times a write, write, read exchange over 127.0.0.1 "
                 "with Nagle on and then with TCP_NODELAY\n";
    return 0;
#endif
}
