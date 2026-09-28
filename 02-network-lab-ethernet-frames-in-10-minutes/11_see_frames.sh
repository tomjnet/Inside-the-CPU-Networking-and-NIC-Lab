#!/usr/bin/env bash
# Ethernet Frames in 10 Minutes - slide 11: real frames: ip link and tcpdump -e.
# Look at: link/ether (your MAC) and mtu in ip link, then "src MAC > dst MAC, ethertype, length" in tcpdump.
set -eu

# Interfaces with MAC address and MTU (output lines are typical)
ip link show
#   2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 state UP
#       link/ether 00:15:5d:a1:b2:c3 brd ff:ff:ff:ff:ff:ff
# Five frames with the link level header (-e), no name lookups (-n)
sudo tcpdump -e -n -c 5 -i eth0
#   00:15:5d:a1:b2:c3 > ff:ff:ff:ff:ff:ff, ethertype ARP (0x0806),
#     length 42: Request who-has 172.20.0.1 tell 172.20.0.2
# The neighbour table that ARP filled: IP address to MAC address
ip neigh show
#   172.20.0.1 dev eth0 lladdr 00:15:5d:0e:0f:10 REACHABLE
