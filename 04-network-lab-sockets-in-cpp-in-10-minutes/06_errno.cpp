// Sockets in C++: Your First Network Program - slide 6: errors: errno and one check function
// Build: make 06_errno
#include <cerrno>
#include <iostream>
#include <system_error>

#if defined(__linux__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
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
#endif

// every socket call returns -1 on failure and sets errno
auto check(auto rc, const char* what) {
    if (rc < 0)
        throw std::system_error(errno, std::generic_category(), what);
    return rc;
}

static void describe(const char* name, int code) {
    std::cout << "  " << name << " = " << code << ": " << std::generic_category().message(code) << "\n";
}

int main() {
    // portable part: the codes of the slide and the text std::system_error gives them
    std::cout << "errno codes a socket program meets (numbers differ between systems):\n";
    describe("ECONNREFUSED", ECONNREFUSED);
    describe("EADDRINUSE", EADDRINUSE);
    describe("EPIPE", EPIPE);
    describe("EINTR", EINTR);
    describe("EAGAIN", EAGAIN);

    std::cout << "check(7, ...) returns " << check(7, "fine") << ": a good return value passes through\n";
    try {
        errno = EADDRINUSE;                     // what a failed bind would have left behind
        check(-1, "bind");
    } catch (const std::system_error& e) {
        std::cout << "check(-1, \"bind\") threw: " << e.what() << " (code " << e.code().value() << ")\n";
    }

#if defined(__linux__)
    try {
        // find a loopback port where nobody listens: bind to port 0, read the port, close again
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        {
            Fd probe{check(socket(AF_INET, SOCK_STREAM, 0), "socket")};
            check(bind(probe.get(), sa(addr), sizeof addr), "bind");
            socklen_t len = sizeof addr;
            check(getsockname(probe.get(), sa(addr), &len), "getsockname");
        }
        std::cout << "connecting to 127.0.0.1 port " << ntohs(addr.sin_port) << ", where nobody listens\n";

        bool refused = false;
        Fd s{check(socket(AF_INET, SOCK_STREAM, 0), "socket")};
        try {
            check(connect(s.get(), sa(addr), sizeof addr), "connect");
        } catch (const std::system_error& e) {      // nobody listens there
            std::cout << e.what() << "\n";          // Connection refused
            refused = e.code().value() == ECONNREFUSED;
        }
        std::cout << (refused ? "the error arrived as an exception with the errno code inside\n"
                              : "expected ECONNREFUSED and did not get it\n");
        return refused ? 0 : 1;
    } catch (const std::system_error& e) {
        std::cout << "setup failed: " << e.what() << "\n";
        return 1;
    }
#else
    std::cout << "this sample needs Linux: it connects to a closed loopback port and prints "
                 "'connect: Connection refused'\n";
    return 0;
#endif
}
