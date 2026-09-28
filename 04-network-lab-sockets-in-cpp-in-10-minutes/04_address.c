/* Sockets in C++: Your First Network Program - slide 4: addresses: sockaddr_in, byte order and port 0 (C version of 04_address.cpp) */
/* Build: make 04_address_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdint.h>
#include <stdio.h>

#if defined(__linux__)
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static struct sockaddr *sa(struct sockaddr_in *a) { return (struct sockaddr *)a; }
#endif

/* what htons does on a little endian machine such as x86: swap the two bytes */
static uint16_t swap_16(uint16_t v) {
    return (uint16_t)(((v & 0xffu) << 8) | ((v >> 8) & 0xffu));
}

int main(void) {
    /* portable part: the classic first bug, a port written without htons */
    const uint16_t port = 8080;
    printf("port %u without htons is read by the network as port %u on a little endian machine\n",
           (unsigned)port, (unsigned)swap_16(port));

#if defined(__linux__)
    printf("htons(8080) = %u, ntohs(htons(8080)) = %u\n", (unsigned)htons(port), (unsigned)ntohs(htons(port)));

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("socket: %s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);              /* zeroed: 16 bytes */
    addr.sin_family = AF_INET;                  /* IPv4 */
    addr.sin_port = htons(0);                   /* 0: kernel picks a port */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);      /* 127.0.0.1 */

    /* bind: one system call; getsockname reads the chosen port back */
    if (bind(fd, sa(&addr), sizeof addr) != 0) printf("bind: %s\n", strerror(errno));
    socklen_t len = sizeof addr;
    if (getsockname(fd, sa(&addr), &len) != 0) printf("getsockname: %s\n", strerror(errno));
    printf("port %u\n", (unsigned)ntohs(addr.sin_port));

    char text[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &addr.sin_addr, text, sizeof text);
    printf("sizeof(sockaddr_in) = %zu bytes, address %s, length reported by getsockname = %u\n",
           sizeof addr, text, (unsigned)len);
    const unsigned char *raw = (const unsigned char *)&addr.sin_addr.s_addr;
    printf("address bytes in memory, network order: %d %d %d %d\n", raw[0], raw[1], raw[2], raw[3]);

    const int ok = ntohs(addr.sin_port) != 0;   /* the kernel replaced the 0 with a real port */
    printf("%s", ok ? "the kernel picked an ephemeral port: run it twice, the number changes\n"
                    : "bind did not assign a port\n");
    close(fd);
    return ok ? 0 : 1;
#else
    printf("this sample needs Linux: it binds a socket to 127.0.0.1 port 0 and prints the port the kernel picked\n");
    return 0;
#endif
}
