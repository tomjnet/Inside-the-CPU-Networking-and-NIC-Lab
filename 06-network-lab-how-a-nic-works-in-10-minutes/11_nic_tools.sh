#!/usr/bin/env bash
# How a NIC Works: RX, TX, DMA and Interrupts - slide 11: in the lab: ethtool and lspci
# Look at: the driver and bus-info of your port, which offloads are on, and the PCIe link speed,
# width and MSI-X vector count of the card. Run it on real Linux hardware: WSL and virtual machines
# show a virtual adapter with no PCIe device behind it.
#
#   bash 11_nic_tools.sh            uses eth0
#   bash 11_nic_tools.sh enp3s0     ip link lists the interface names of your machine
set -eu

IFACE="${1:-eth0}"

# Which driver and firmware run this port (typical output)
ethtool -i "$IFACE"
#   driver: ixgbe   firmware-version: 0x800007   bus-info: 0000:03:00.0
# Which offloads are on: checksum, TSO, receive hashing (RSS)
ethtool -k "$IFACE" | grep -E 'checksum|segmentation|receive-hash' || true
#   rx-checksumming: on   tcp-segmentation-offload: on
# The card on the PCIe bus: link speed, width, MSI-X vectors
lspci -nn | grep -i ethernet || echo "no PCIe Ethernet device here (WSL or a virtual machine?)"
# the slide shows 03:00.0; here the address comes from the bus-info line of ethtool -i
BDF="$(ethtool -i "$IFACE" | awk '/^bus-info:/ {print $2}')"
if [ -n "$BDF" ]; then
  sudo lspci -vv -s "$BDF" | grep -E 'LnkSta:|MSI-X' || true
fi
#   LnkSta: Speed 8GT/s, Width x8     MSI-X: Enable+ Count=64
