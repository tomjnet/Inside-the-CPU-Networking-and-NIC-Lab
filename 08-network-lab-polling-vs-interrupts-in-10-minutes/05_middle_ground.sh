#!/usr/bin/env bash
# Polling vs Interrupts: Why Low Latency Systems Poll - slide 5: the middle ground: coalescing, NAPI, busy poll
# Look at rx-usecs and rx-frames before and after, and at the third column of softnet_stat
# (time squeeze: the NAPI poll ran out of budget). Replace eth0 with your interface (ip link).
# Needs a real NIC and root; WSL and most virtual NICs refuse the ethtool -C line.
# Per socket instead of system wide: setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &usecs, sizeof usecs).
set -eu

# interrupt coalescing: one interrupt per 50 us or per 32 frames
ethtool -c eth0
#   rx-usecs: 50   rx-frames: 32   adaptive-rx: on   (typical)
# lowest latency, most interrupts: fire on every frame
sudo ethtool -C eth0 adaptive-rx off rx-usecs 0 rx-frames 1
# NAPI: one interrupt, then the kernel polls inside a softirq
cat /proc/net/softnet_stat
# busy polling: recv spins on the queue for 50 us before it sleeps
sudo sysctl -w net.core.busy_read=50
sudo sysctl -w net.core.busy_poll=50
