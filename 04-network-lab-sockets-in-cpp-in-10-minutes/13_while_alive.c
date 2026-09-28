/* Sockets in C++: Your First Network Program - slide 13: thank you (C version of 13_while_alive.cpp) */
/* Build: make 13_while_alive_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>

/* the imaginary "tomjnet.h" of the slide */
typedef struct { char name[80]; } Topic;
static int episodes = 0;
static int alive(void) { return episodes < 3; }             /* three episodes, then the demo ends */
static Topic next_network_topic(void) {
    static const char *const topics[] = {"Inside the Linux Network Stack: From Socket to NIC",
                                         "How a NIC Works: RX, TX, DMA and Interrupts",
                                         "NIC Ring Buffers and Descriptor Queues Explained"};
    Topic t;
    snprintf(t.name, sizeof t.name, "%s", topics[episodes++]);
    return t;
}
static Topic viewer_request(void) {
    Topic t;
    snprintf(t.name, sizeof t.name, "viewer request #%d", episodes);
    return t;
}
static void subscribe(void) { printf("  subscribed to TomJNet\n"); }

typedef struct {
    const char *peer;
    int sent;
} Channel;
static void channel_send(Channel *c, Topic t) {             /* C has no member functions: the object is a parameter */
    printf("  send to %s: %s\n", c->peer, t.name);
    ++c->sent;
}
static Channel connect_to(const char *name) {
    printf("connected to %s\n", name);
    Channel c;
    c.peer = name;
    c.sent = 0;
    return c;
}

int main(void) {
    Channel viewers = connect_to("TomJNet");        /* one handshake */
    while (alive()) {
        channel_send(&viewers, next_network_topic());   /* the network stack */
        channel_send(&viewers, viewer_request());       /* blocking, worth it */
        subscribe();                                    /* lifetime benefit */
    }

    printf("%d topics sent over one connection\n", viewers.sent);
    return 0;
}
