#!/bin/sh
cat <<EOF > /etc/udhcpc.config.high_port
HIGH_PORT_DONGLE=$boot_file
HIGH_PORT_SERVER=${siaddr:-$serverid}
HIGH_PORT_IP=$ip
HIGH_PORT_ETH=$interface
HIGH_PORT_HNAME=$hostname
HIGH_PORT_NTP="$ntpsrv"
EOF
