#!/bin/bash

modprobe pktgen
ifconfig tun1 up
ifconfig tun2 up

iptables -t raw -A PREROUTING -i tun2 -j DROP

echo "add_device tun1" > /proc/net/pktgen/kpktgend_0
echo "count 1000000000" > /proc/net/pktgen/tun1
echo "src_min 10.0.0.1" > /proc/net/pktgen/tun1
echo "src_max 100.0.0.1" > /proc/net/pktgen/tun1
echo "pkt_size 1400" > /proc/net/pktgen/tun1
echo "queue_map_min 1" > /proc/net/pktgen/tun1
echo "queue_map_max 100" > /proc/net/pktgen/tun1
#echo "flag UDPSRC_RND" >/proc/net/pktgen/tun1
uname -r
echo "start" > /proc/net/pktgen/pgctrl
