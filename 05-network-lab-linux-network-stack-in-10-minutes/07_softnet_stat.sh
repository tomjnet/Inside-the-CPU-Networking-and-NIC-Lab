#!/usr/bin/env bash
# Look at: column 2 (dropped) and column 3 (time_squeeze) of each CPU row, which rows grow, and
# any drop, miss or error counter of the NIC. Replace eth0 with your interface name.
set -eu

# one row per CPU, hex: processed, dropped, time_squeeze
cat /proc/net/softnet_stat
#   0001a3f2 00000000 00000004 00000000              (typical, CPU 0)
#   00000c10 00000000 00000000 00000000              (typical, CPU 1)

# NET_RX and NET_TX softirq runs per CPU
grep -E 'NET_RX|NET_TX' /proc/softirqs

# the NIC's own counters: look at drops, misses and errors
ethtool -S eth0 | grep -E 'drop|miss|err'
#   rx_missed_errors: 0                              (typical)
