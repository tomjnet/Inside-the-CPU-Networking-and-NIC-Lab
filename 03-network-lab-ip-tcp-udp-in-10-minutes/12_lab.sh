#!/usr/bin/env bash
# Look at: the State column of ss -tn (ESTAB after the handshake), Recv-Q and Send-Q staying at 0, and the
# four numbers of each connection. In traceroute: one line per router, each found by a probe whose TTL ran out.
set -eu

# TCP sockets of this machine: state, queues, both addresses
ss -tn
#   State  Recv-Q Send-Q  Local Address:Port   Peer Address:Port
#   ESTAB  0      0       192.168.1.20:51514   140.82.112.26:443
# UDP sockets: no handshake, so the state column says little
ss -un
# every router on the way, found with TTL 1, 2, 3 and so on
traceroute -n 1.1.1.1
#   1  192.168.1.1   0.6 ms     (typical: yours will differ)
#   2  10.20.0.1     8.1 ms
