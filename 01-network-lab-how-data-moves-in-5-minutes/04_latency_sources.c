/* Computer Networking in 5 Minutes: How Data Moves Between Machines - slide 4: where latency comes from (C version of 04_latency_sources.cpp) */
/* Build: make 04_latency_sources_c */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>

/* serialization: push every bit onto the wire, bits / link rate */
static double serialize_us(double bytes, double gbps) {
    return bytes * 8.0 / (gbps * 1e9) * 1e6;
}
/* propagation: about 200000 km/s in fiber or copper, 5 us per km */
static double propagate_us(double km) { return km / 200000.0 * 1e6; }
/* 146 B at 1 Gbps: 1.2 us; at 10 Gbps: 0.12 us */
/* 100 m of cable: 0.5 us; 1000 km of fiber: 5000 us, one way */
/* queueing and the two hosts' stacks: tens of us, and they vary */

int main(void) {
    printf("serialization of one 146 B frame (100 B of data over UDP)\n");
    const double rates_gbps[] = {0.1, 1.0, 10.0, 25.0, 100.0};
    for (size_t i = 0; i < sizeof rates_gbps / sizeof rates_gbps[0]; ++i) {
        const double gbps = rates_gbps[i];
        printf("  %6.1f Gbps: %8.3f us\n", gbps, serialize_us(146.0, gbps));
    }

    printf("\npropagation, one way\n");
    const double distances_km[] = {0.001, 0.1, 1.0, 100.0, 1000.0};
    for (size_t i = 0; i < sizeof distances_km / sizeof distances_km[0]; ++i) {
        const double km = distances_km[i];
        printf("  %9.3f km: %10.3f us\n", km, propagate_us(km));
    }

    /* One way budget inside a data center. The first two lines are computed; the last two are
       typical orders of magnitude that we assume here, not measurements: later episodes measure them. */
    const double serialization = serialize_us(146.0, 10.0);
    const double propagation = propagate_us(0.1);
    const double queueing_typical = 5.0;          /* one or two switch hops with light load */
    const double host_stacks_typical = 2 * 15.0;  /* sender plus receiver: system call, kernel, driver, NIC */
    const double total = serialization + propagation + queueing_typical + host_stacks_typical;

    printf("\none way budget: 146 B, 10 Gbps, 100 m of cable (typical values, assumed)\n");
    printf("  serialization %7.2f us  (computed)\n", serialization);
    printf("  propagation   %7.2f us  (computed)\n", propagation);
    printf("  queueing      %7.2f us  (typical, varies: the jitter)\n", queueing_typical);
    printf("  host stacks   %7.2f us  (typical, both hosts)\n", host_stacks_typical);
    printf("  total         %7.2f us, of which the hosts are %.0f percent\n",
           total, host_stacks_typical / total * 100.0);
    printf("the wire is fast; the hosts are the part this playlist learns to shrink\n");
    return 0;
}
