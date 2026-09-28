// Sockets in C++: Your First Network Program - slide 9: tcp client: connect, send and a byte stream
// Build: make 09_tcp_client
#include <iostream>

#if defined(__linux__)
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

// TCP is a byte stream: one recv may return part of a message
void recv_all(int fd, char* p, std::size_t want) {
    while (want > 0) {                      // one system call per turn
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

// the echo server of slide 8 in its own thread. The first message comes back in one send; the second
// one on purpose in two pieces, 5 bytes and then 8, the way a slow or busy network may deliver it.
static void server(int lis, std::string& error_out) {
    try {
        Fd conn{check(accept(lis, nullptr, nullptr), "accept")};
        give_up_after(conn.get(), 5);
        int one = 1;
        check(setsockopt(conn.get(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof one), "setsockopt");
        char buf[13];
        recv_all(conn.get(), buf, sizeof buf);
        check(send(conn.get(), buf, sizeof buf, MSG_NOSIGNAL), "send");
        recv_all(conn.get(), buf, sizeof buf);
        check(send(conn.get(), buf, 5, MSG_NOSIGNAL), "send");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        check(send(conn.get(), buf + 5, 8, MSG_NOSIGNAL), "send");
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
            char reply[13];

            Fd cli{check(socket(AF_INET, SOCK_STREAM, 0), "socket")};
            check(connect(cli.get(), sa(addr), sizeof addr), "connect");
            check(send(cli.get(), "hello, socket", 13, MSG_NOSIGNAL), "send");
            recv_all(cli.get(), reply, 13);             // blocks until all 13

            give_up_after(cli.get(), 5);
            std::cout << "connected to 127.0.0.1 port " << ntohs(addr.sin_port) << ", reply 1: '"
                      << std::string(reply, sizeof reply) << "'\n";
            ok = std::string(reply, sizeof reply) == "hello, socket";

            // the third classic bug: one plain recv, and the message the server sends in two pieces
            check(send(cli.get(), "hello, stream", 13, MSG_NOSIGNAL), "send");
            auto first = check(recv(cli.get(), reply, sizeof reply, 0), "recv");
            std::cout << "reply 2, one plain recv: " << first << " of 13 bytes: '"
                      << std::string(reply, static_cast<std::size_t>(first)) << "' (this run)\n";
            const std::size_t have = static_cast<std::size_t>(first);
            if (have < sizeof reply) recv_all(cli.get(), reply + have, sizeof reply - have);
            std::cout << "reply 2 after recv_all:  13 of 13 bytes: '" << std::string(reply, sizeof reply) << "'\n";
            ok = ok && std::string(reply, sizeof reply) == "hello, stream";
        } catch (const std::exception& e) {
            std::cout << "client: " << e.what() << "\n";
            ok = false;
        }
        t.join();

        if (!server_error.empty()) std::cout << "server: " << server_error << "\n";
        std::cout << "a recv that returns less than the message is normal TCP: frame your messages and loop\n";
        return ok && server_error.empty() ? 0 : 1;
    } catch (const std::system_error& e) {
        std::cout << "setup failed: " << e.what() << "\n";
        return 1;
    }
#else
    std::cout << "this sample needs Linux: a TCP client whose reply arrives in two pieces, "
                 "read once with a plain recv and once with recv_all\n";
    return 0;
#endif
}
