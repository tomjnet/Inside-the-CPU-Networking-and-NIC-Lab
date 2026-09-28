/* How a NIC Works: RX, TX, DMA and Interrupts - slide 8: the rx timeline: run the model (C version of 08_rx_timeline.cpp) */
/* Build: make 08_rx_timeline_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>

/* A model of the receive path as discrete steps. Every cost is a typical order of magnitude, not a
   measurement: change the numbers to the ones of your own card and link, and run it again. */
typedef struct { const char *where; const char *what; double ns; } Step;

static void print_row(double t, const Step *s) {
    printf("  t = %7.0f ns  %-8s %-40s +%6.0f ns\n", t, s->where, s->what, s->ns);
}

/* time on the wire: preamble (8 B) and inter frame gap (12 B) travel with every frame */
static double wire_ns(double frame_bytes, double gbit_per_s) {
    return (frame_bytes + 20.0) * 8.0 / gbit_per_s;
}

/* Typical orders of magnitude for a 10 GbE port, not measurements */
static const Step kRxPath[] = {
    {"PHY+MAC", "1500 B frame off the wire, FCS check", 1200},
    {"RX FIFO", "wait for the DMA engine",               100},
    {"PCIe",    "DMA write of the payload",              600},
    {"PCIe",    "DMA write of the descriptor",           300},
    {"MSI-X",   "interrupt reaches the core",           2000},
    {"driver",  "NAPI poll, sk_buff, hand to stack",    3000},
};

static double software_share(const Step *steps, int n, double *total_out) {
    double total = 0, software = 0;
    for (int i = 0; i < n; ++i) {
        total += steps[i].ns;
        if (i >= n - 2) software += steps[i].ns;   /* the last two rows are the only ones that use the CPU */
    }
    *total_out = total;
    return 100.0 * software / total;
}

int main(void) {
    printf("RX path of a 1500 byte frame on 10 GbE (typical costs):\n");
    double t = 0;   /* CPU work starts only in the last two rows */
    for (int i = 0; i < 6; ++i) { print_row(t, &kRxPath[i]); t += kRxPath[i].ns; }

    double total = 0;
    double share = software_share(kRxPath, 6, &total);
    printf("  total %.0f ns, of which interrupt plus driver: %.0f percent\n", total, share);
    printf("  the CPU executes nothing for this packet during the first %.0f ns\n\n",
           kRxPath[0].ns + kRxPath[1].ns + kRxPath[2].ns + kRxPath[3].ns);

    /* the same path for a small frame: the wire time is computed, the DMA write is shorter */
    Step small_path[] = {
        {"PHY+MAC", "64 B frame off the wire, FCS check", 0},
        {"RX FIFO", "wait for the DMA engine",             100},
        {"PCIe",    "DMA write of the payload",            300},
        {"PCIe",    "DMA write of the descriptor",         300},
        {"MSI-X",   "interrupt reaches the core",         2000},
        {"driver",  "NAPI poll, sk_buff, hand to stack",  3000},
    };
    small_path[0].ns = wire_ns(64, 10);   /* C initializers of this kind must be constants: set it here */
    printf("RX path of a 64 byte frame on 10 GbE (typical costs):\n");
    double t2 = 0;
    for (int i = 0; i < 6; ++i) { print_row(t2, &small_path[i]); t2 += small_path[i].ns; }
    double total2 = 0;
    double share2 = software_share(small_path, 6, &total2);
    printf("  total %.0f ns, of which interrupt plus driver: %.0f percent\n\n", total2, share2);

    printf("time on the wire by frame size and link speed (frame + preamble + gap):\n");
    const double sizes[] = {64, 590, 1500, 9000};
    const double speeds[] = {1, 10, 25, 100};
    printf("  %10s", "bytes");
    for (int j = 0; j < 4; ++j) printf("  %7.0f GbE", speeds[j]);
    printf("\n");
    for (int i = 0; i < 4; ++i) {
        printf("  %10.0f", sizes[i]);
        for (int j = 0; j < 4; ++j) printf("  %8.0f ns", wire_ns(sizes[i], speeds[j]));
        printf("\n");
    }
    printf("the smaller the frame and the faster the link, the more the software share dominates\n");

    /* correctness: the model's 1200 ns is the rounded wire time of a full frame at 10 GbE */
    double full = wire_ns(1500, 10);
    return (full > 1100 && full < 1300 && share2 > share) ? 0 : 1;
}
