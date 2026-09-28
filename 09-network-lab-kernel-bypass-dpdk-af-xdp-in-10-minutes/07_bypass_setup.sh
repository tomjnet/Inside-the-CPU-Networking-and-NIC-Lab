#!/usr/bin/env bash
# Kernel Bypass: DPDK, AF_XDP and Low Latency Networking - slide 7: the setup, to read and not to run
# Look at: the NIC leaving "ip link" after the DPDK bind, and staying there with the XDP program attached.
# Needs root, a supported NIC and real hardware (not WSL, not a virtual machine). Never bind the NIC that
# carries your SSH session. Edit the PCIe address (0000:03:00.0) and the interface (eth0) first.
set -eu

if [ "${BYPASS_LAB:-}" != "yes" ]; then
    echo "read this script first: it takes a NIC away from the kernel"
    echo "after editing the PCIe address and the interface: BYPASS_LAB=yes $0"
    exit 0
fi

# DPDK: reserve huge pages, take the NIC away from the kernel
sudo sysctl -w vm.nr_hugepages=1024
sudo modprobe vfio-pci
sudo dpdk-devbind.py --bind=vfio-pci 0000:03:00.0
# two pinned polling cores, the NIC is gone from ip link
sudo dpdk-testpmd -l 2-3 -n 4 -- --forward-mode=rxonly
# AF_XDP: the kernel keeps the NIC, one RX queue is redirected
sudo ethtool -L eth0 combined 4
sudo ethtool -N eth0 flow-type udp4 dst-port 9000 action 2
sudo ip link set dev eth0 xdpdrv obj xdp_redirect.o sec xdp
# undo: detach the XDP program
sudo ip link set dev eth0 xdp off
