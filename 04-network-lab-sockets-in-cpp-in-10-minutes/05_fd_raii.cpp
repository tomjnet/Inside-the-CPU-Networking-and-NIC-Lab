// Sockets in C++: Your First Network Program - slide 5: raii: a descriptor that closes itself
// Build: make 05_fd_raii
#include <iostream>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

class Fd {                                  // owns one descriptor
    int fd_ = -1;
public:
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) close(fd_); }     // one system call
    Fd(const Fd&) = delete;                 // two owners: double close
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
    int get() const { return fd_; }
};

static_assert(!std::is_copy_constructible_v<Fd>, "a descriptor has one owner");
static_assert(std::is_nothrow_move_constructible_v<Fd>, "but it can change hands");
static_assert(sizeof(Fd) == sizeof(int), "and it costs nothing: the object is one int");

// true while the number still names an open descriptor of this process
static bool is_open(int fd) { return fcntl(fd, F_GETFD) != -1 || errno != EBADF; }

static void fails_half_way(int& seen) {
    Fd s{socket(AF_INET, SOCK_DGRAM, 0)};
    seen = s.get();
    throw std::runtime_error("error path");  // no close() written anywhere: the destructor runs
}
#endif

int main() {
#if defined(__linux__)
    int number = -1;
    {
        Fd sock{socket(AF_INET, SOCK_STREAM, 0)};   // closed on every path
        number = sock.get();
        std::cout << "socket() returned descriptor " << number << " (0, 1 and 2 are stdin, stdout, stderr)\n";
        std::cout << "open inside the scope: " << (is_open(number) ? "yes" : "no") << "\n";

        Fd moved{std::move(sock)};
        std::cout << "after the move: source holds " << sock.get() << ", target holds " << moved.get() << "\n";
        if (sock.get() != -1 || moved.get() != number) return 1;
    }
    const bool closed_by_scope = !is_open(number);
    std::cout << "open after the scope:  " << (closed_by_scope ? "no" : "yes") << " (closed exactly once)\n";

    int seen = -1;
    try {
        fails_half_way(seen);
    } catch (const std::runtime_error& e) {
        std::cout << "exception '" << e.what() << "' left the function, descriptor " << seen << " open: "
                  << (is_open(seen) ? "yes" : "no") << "\n";
    }
    const bool closed_by_unwind = !is_open(seen);
    std::cout << "sizeof(Fd) = " << sizeof(Fd) << " bytes, the same as an int\n";
    return closed_by_scope && closed_by_unwind ? 0 : 1;
#else
    std::cout << "this sample needs Linux: it shows a socket descriptor closed by a destructor, "
                 "on the normal path, after a move and after an exception\n";
    return 0;
#endif
}
