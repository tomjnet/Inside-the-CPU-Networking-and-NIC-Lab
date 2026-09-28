// Sockets in C++: Your First Network Program - slide 7: udp echo: datagrams in, datagrams out
// Build: make 07_udp_echo
#include <iostream>
#include <string>

#if defined(__linux__)
#include <arpa/inet.h>
#include <cerrno>
#include <functional>
#include <netinet/in.h>
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

// a blocking call that waits longer than this fails with EAGAIN: the demo can never hang
static void give_up_after(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv), "setsockopt");
}

// the client of the slide's comment, in its own thread: one datagram out, one datagram back
static void client(sockaddr_in addr, std::string& reply_out, std::string& error_out) {
    try {
        Fd cli_fd{check(socket(AF_INET, SOCK_DGRAM, 0), "socket")};
        give_up_after(cli_fd.get(), 5);
        const int cli = cli_fd.get();
        char reply[1500];
        check(sendto(cli, "ping", 4, 0, sa(addr), sizeof addr), "sendto");
        auto got = check(recvfrom(cli, reply, sizeof reply, 0, nullptr, nullptr), "recvfrom");
        reply_out.assign(reply, static_cast<std::size_t>(got));
    } catch (const std::system_error& e) {
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

        // server: no listen, no accept, one datagram in, one datagram out
        Fd srv{check(socket(AF_INET, SOCK_DGRAM, 0), "socket")};
        check(bind(srv.get(), sa(addr), sizeof addr), "bind");

        // (added) read the port back, then start the client; its datagram waits in the receive buffer
        socklen_t alen = sizeof addr;
        check(getsockname(srv.get(), sa(addr), &alen), "getsockname");
        give_up_after(srv.get(), 5);
        std::cout << "UDP echo server on 127.0.0.1 port " << ntohs(addr.sin_port) << "\n";
        std::string reply_text, client_error;
        std::thread t(client, addr, std::ref(reply_text), std::ref(client_error));

        try {
            char buf[1500];
            sockaddr_in from{};
            socklen_t flen = sizeof from;
            auto n = check(recvfrom(srv.get(), buf, sizeof buf, 0,
                                    sa(from), &flen), "recvfrom");      // blocks
            check(sendto(srv.get(), buf, n, 0, sa(from), flen), "sendto");

            // client: sendto(cli, "ping", 4, 0, sa(addr), sizeof addr);
            //         recvfrom(cli, reply, sizeof reply, 0, nullptr, nullptr);

            std::cout << "server: one datagram of " << n << " bytes from port " << ntohs(from.sin_port)
                      << ", sent back with one sendto\n";
        } catch (const std::system_error& e) {
            std::cout << "server: " << e.what() << "\n";
        }
        t.join();

        if (!client_error.empty()) std::cout << "client: " << client_error << "\n";
        std::cout << "client: reply '" << reply_text << "' (" << reply_text.size()
                  << " bytes: the datagram kept its boundaries)\n";
        std::cout << "system calls for the whole exchange: 2 on the client, 2 on the server, plus setup\n";
        return reply_text == "ping" ? 0 : 1;
    } catch (const std::system_error& e) {
        std::cout << "setup failed: " << e.what() << "\n";
        return 1;
    }
#else
    std::cout << "this sample needs Linux: a UDP echo server and its client over 127.0.0.1, "
                 "two threads in one process, reply 'ping'\n";
    return 0;
#endif
}
