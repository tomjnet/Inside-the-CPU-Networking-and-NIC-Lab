#!/usr/bin/env bash
# NIC Ring Buffers and Descriptor Queues Explained - slide 11: in the lab: ethtool
# Look at: the RX ring size against its maximum, the number of channels (rings), and whether a missed or
# drop counter grows while traffic runs. Replace eth0 with your interface (ip link). Real hardware only:
# WSL and most virtual machines show a virtual NIC with no real rings.
set -eu

# ring sizes: the hardware maximum and the current setting
ethtool -g eth0
#   Pre-set maximums:   RX: 4096   TX: 4096       (typical)
#   Current settings:   RX: 512    TX: 512
# a larger RX ring: fewer drops in a burst, more queueing
sudo ethtool -G eth0 rx 2048
# channels: how many rings the NIC runs in parallel
ethtool -l eth0
#   Current settings:   Combined: 8               (typical)
# drops since boot: the counter names depend on the driver
ethtool -S eth0 | grep -i -E 'miss|drop|no_buf'
