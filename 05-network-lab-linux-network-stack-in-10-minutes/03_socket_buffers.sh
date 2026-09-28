#!/usr/bin/env bash
# Look at: the skmem line of each connection: rb is the receive buffer limit, tb the send
# buffer limit, t and r the bytes queued right now; then the min, default and max of the sysctl.
set -eu

# socket buffers of every TCP connection: -t tcp, -m memory, -i info
ss -tmi
#   ESTAB 0 0 127.0.0.1:40112 127.0.0.1:5000        (typical)
#     skmem:(r0,rb131072,t0,tb2626560,f0,w0,o0,bl0,d0)
#     cubic rtt:0.04/0.02 mss:32768 cwnd:10 bytes_sent:4096

# limits of the send and receive buffers: min, default, max in bytes
sysctl net.ipv4.tcp_wmem net.ipv4.tcp_rmem
#   net.ipv4.tcp_wmem = 4096 16384 4194304           (typical)
#   net.ipv4.tcp_rmem = 4096 131072 6291456
