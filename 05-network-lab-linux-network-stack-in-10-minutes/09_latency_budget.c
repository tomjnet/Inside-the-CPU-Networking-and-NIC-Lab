/* Inside the Linux Network Stack: From Socket to NIC - slide 9: a latency budget in c++ (C version of 09_latency_budget.cpp) */
/* Build: make 09_latency_budget_c */
/* */
/* A model with typical orders of magnitude, not a measurement: change the numbers and reason about the path. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdio.h>

typedef struct { const char *name; double ns; int copies; int switches; } Stage;

int main(void) {
    const Stage rx_path[] = {               /* typical orders of magnitude */
        {"hard IRQ, schedule NAPI",   2000, 0, 0},
        {"softirq: NAPI poll, driver", 3000, 0, 0},
        {"IP and TCP or UDP",          2000, 0, 0},
        {"socket buffer, wake up",     5000, 0, 1},  /* context switch */
        {"recv: system call + copy",   1000, 1, 0},  /* kernel to user */
    };
    const size_t n = sizeof rx_path / sizeof rx_path[0];
    double total = 0;
    for (size_t i = 0; i < n; ++i) total += rx_path[i].ns;    /* about 13 us */
    /* the wire time of a short frame at 10 Gb/s: under 0.1 us */

    printf("receive path, typical costs (a model, not a measurement)\n");
    printf("  %-30s%9s%9s%8s%10s\n", "stage", "us", "share", "copies", "switches");
    int copies = 0;
    int switches = 0;
    for (size_t i = 0; i < n; ++i) {
        const Stage *s = &rx_path[i];
        printf("  %-30s%9.1f%8.1f%%%8d%10d\n", s->name, s->ns / 1000.0, 100.0 * s->ns / total, s->copies, s->switches);
        copies += s->copies;
        switches += s->switches;
    }
    printf("  %-30s%9.1f%8.1f%%%8d%10d\n\n", "total", total / 1000.0, 100.0, copies, switches);

    /* what if: the questions of the slide, answered by the same table */
    const size_t irq = 0;
    const size_t wake = 3;
    const double no_wake = total - rx_path[wake].ns;                 /* a thread already spins on the core */
    const double no_wake_no_irq = no_wake - rx_path[irq].ns;         /* and it polls, so no interrupt either */
    printf("what if a thread is already spinning (no wake up): %.1f us, %.1f%% saved\n",
           no_wake / 1000.0, 100.0 * (total - no_wake) / total);
    printf("what if it also polls the NIC (no interrupt):      %.1f us, %.1f%% saved\n",
           no_wake_no_irq / 1000.0, 100.0 * (total - no_wake_no_irq) / total);

    /* the wire: 64 byte frame + 8 bytes of preamble + 12 bytes of gap = 84 bytes = 672 bits */
    const double wire_ns = 84.0 * 8.0 / 10e9 * 1e9;
    printf("wire time of a short frame at 10 Gb/s: %.1f ns, %.0f times less than the receive path of the host\n",
           wire_ns, total / wire_ns);
    return 0;
}
