// Sockets in C++: Your First Network Program - slide 4: addresses: sockaddr_in, byte order and port 0
// Build: make 04_address
#include <cstdint>
#include <iostream>

#if defined(__linux__)
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

sockaddr* sa(sockaddr_in& a) { return reinterpret_cast<sockaddr*>(&a); }
#endif

// what htons does on a little endian machine such as x86: swap the two bytes
static std::uint16_t swap_16(std::uint16_t v) {
    return static_cast<std::uint16_t>(((v & 0xffu) << 8) | ((v >> 8) & 0xffu));
}

int main() {
    // portable part: the classic first bug, a port written without htons
    const std::uint16_t port = 8080;
    std::cout << "port " << port << " without htons is read by the network as port " << swap_16(port)
              << " on a little endian machine\n";

#if defined(__linux__)
    std::cout << "htons(8080) = " << htons(port) << ", ntohs(htons(8080)) = " << ntohs(htons(port)) << "\n";

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cout << "socket: " << std::strerror(errno) << "\n";
        return 1;
    }

    sockaddr_in addr{};                         // zeroed: 16 bytes
    addr.sin_family = AF_INET;                  // IPv4
    addr.sin_port = htons(0);                   // 0: kernel picks a port
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);      // 127.0.0.1

    // bind: one system call; getsockname reads the chosen port back
    bind(fd, sa(addr), sizeof addr);
    socklen_t len = sizeof addr;
    getsockname(fd, sa(addr), &len);
    std::cout << "port " << ntohs(addr.sin_port) << "\n";

    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr.sin_addr, text, sizeof text);
    std::cout << "sizeof(sockaddr_in) = " << sizeof addr << " bytes, address " << text
              << ", length reported by getsockname = " << len << "\n";
    const unsigned char* raw = reinterpret_cast<const unsigned char*>(&addr.sin_addr.s_addr);
    std::cout << "address bytes in memory, network order: " << int(raw[0]) << " " << int(raw[1]) << " "
              << int(raw[2]) << " " << int(raw[3]) << "\n";

    const bool ok = ntohs(addr.sin_port) != 0;  // the kernel replaced the 0 with a real port
    std::cout << (ok ? "the kernel picked an ephemeral port: run it twice, the number changes\n"
                     : "bind did not assign a port\n");
    close(fd);
    return ok ? 0 : 1;
#else
    std::cout << "this sample needs Linux: it binds a socket to 127.0.0.1 port 0 and prints the port the kernel picked\n";
    return 0;
#endif
}
