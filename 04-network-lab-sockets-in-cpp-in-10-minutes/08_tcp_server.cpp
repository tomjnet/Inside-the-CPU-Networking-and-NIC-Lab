// Sockets in C++: Your First Network Program - slide 8: tcp echo server: listen, accept, recv, send
// Build: make 08_tcp_server
#include <iostream>

#if defined(__linux__)
#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <functional>
#include <netinet/in.h>
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

// a blocking call that waits longer than this fails with EAGAIN: the demo can never hang
static void give_up_after(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    check(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv), "setsockopt");
}

// a small client in its own thread: connect, send three messages, read every byte back, close
static void client(sockaddr_in addr, std::string& echoed, std::string& error_out) {
    try {
        Fd cli{check(socket(AF_INET, SOCK_STREAM, 0), "socket")};
        give_up_after(cli.get(), 5);
        check(connect(cli.get(), sa(addr), sizeof addr), "connect");
        for (const char* msg : {"hello, ", "echo ", "server"}) {
            const std::string text = msg;
            check(send(cli.get(), text.data(), text.size(), MSG_NOSIGNAL), "send");
            std::size_t got = 0;
            char buf[64];
            while (got < text.size()) {
                auto n = check(recv(cli.get(), buf, sizeof buf, 0), "recv");
                if (n == 0) return;
                echoed.append(buf, static_cast<std::size_t>(n));
                got += static_cast<std::size_t>(n);
            }
        }
    } catch (const std::system_error& e) {
        error_out = e.what();
    }                                           // cli closes here: the server's recv returns 0
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
        check(listen(lis.get(), 16), "listen");     // backlog: 16 pending

        // (added) read the port back, then start the client; its handshake waits in the backlog
        socklen_t alen = sizeof addr;
        check(getsockname(lis.get(), sa(addr), &alen), "getsockname");
        give_up_after(lis.get(), 5);
        std::cout << "TCP echo server listening on 127.0.0.1 port " << ntohs(addr.sin_port)
                  << ", descriptor " << lis.get() << "\n";
        std::string echoed, client_error;
        std::thread t(client, addr, std::ref(echoed), std::ref(client_error));

        long long calls = 0, bytes = 0;
        try {
            // accept blocks, then returns a NEW descriptor for this client
            Fd conn{check(accept(lis.get(), nullptr, nullptr), "accept")};
            give_up_after(conn.get(), 5);
            std::cout << "accept returned descriptor " << conn.get() << ": the listening one stays open\n";
            char buf[4096];
            for (;;) {
                auto n = check(recv(conn.get(), buf, sizeof buf, 0), "recv");
                if (n == 0) break;                      // 0: the peer closed
                check(send(conn.get(), buf, n, MSG_NOSIGNAL), "send");
                ++calls;
                bytes += n;
            }
            std::cout << "recv returned 0: the client closed, the loop ends, both descriptors close themselves\n";
        } catch (const std::system_error& e) {
            std::cout << "server: " << e.what() << "\n";
        }
        t.join();

        if (!client_error.empty()) std::cout << "client: " << client_error << "\n";
        std::cout << "server echoed " << bytes << " bytes in " << calls << " recv and send pairs\n";
        std::cout << "client got back: '" << echoed << "'\n";
        return echoed == "hello, echo server" ? 0 : 1;
    } catch (const std::system_error& e) {
        std::cout << "setup failed: " << e.what() << "\n";
        return 1;
    }
#else
    std::cout << "this sample needs Linux: a TCP echo server (socket, bind, listen, accept, recv, send) "
                 "and a client thread over 127.0.0.1\n";
    return 0;
#endif
}
