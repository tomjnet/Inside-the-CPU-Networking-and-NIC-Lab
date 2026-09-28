#!/usr/bin/env bash
# Look at: the link/ether (MAC) and inet (IP) lines of each interface, the default route,
# and the time= column of ping over loopback: the cost of the host's own stack, there and back.
set -eu

# interfaces: the MAC (link/ether) and the IP (inet) of each one
ip addr
#   2: eth0: <BROADCAST,MULTICAST,UP> mtu 1500         (typical)
#      link/ether 00:15:5d:01:02:03
#      inet 192.168.1.20/24
# routing table: everything that is not local goes to the router
ip route
#   default via 192.168.1.1 dev eth0
# round trip over loopback: the host's own stack, no wire, no NIC
ping -c 3 127.0.0.1
#   64 bytes from 127.0.0.1: icmp_seq=1 ttl=64 time=0.03 ms
