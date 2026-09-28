#!/usr/bin/env bash
# Look at: dropped, backlog and requeues of the qdisc (waiting here is latency), then the TX
# counters and qlen of the interface. Replace eth0 with your interface name (see: ip link).
set -eu

# the queueing discipline in front of the driver: sent, dropped, backlog
tc -s qdisc show dev eth0
#   qdisc fq_codel 0: root refcnt 2 limit 10240p     (typical)
#    Sent 1839412 bytes 12904 pkt (dropped 0, overlimits 0 requeues 3)
#    backlog 0b 0p requeues 3

# the interface counters and the length of the transmit queue
ip -s link show dev eth0
#   2: eth0: mtu 1500 qdisc fq_codel state UP qlen 1000
#      TX: bytes 1839412 packets 12904 errors 0 dropped 0
