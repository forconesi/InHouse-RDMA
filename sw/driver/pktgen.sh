#!/bin/bash

delay=0
if [ $# -lt 2 ]; then
	echo "Usage: $0 <netif> <packet size> [delay(=$delay)]"
	exit
fi
netif=$1
pkt_size=$2
if [ $# -ge 3 ]; then
	delay=$3
fi

function pgset() {
	local result
	echo $1 > $PGDEV

	result=`cat $PGDEV | grep "Result: OK:"`
	if [ "$result" = "" ]; then
		cat $PGDEV | grep Result:
	fi
}

function pg() {
	echo inject > $PGDEV
	cat $PGDEV
}

if [[ ! "$(lsmod)" =~ pktgen ]]; then
	modprobe pktgen
fi

PGDEV=/proc/net/pktgen/kpktgend_0
if [ ! -e $PGDEV ]; then
	echo "Error: pktgen might not be loaded!"
	exit
fi
echo "Removing all devices"
pgset "rem_device_all"
echo "Adding $netif"
pgset "add_device $netif"
echo "Setting max_before_softirq 10000"
pgset "max_before_softirq 10000"

PGDEV=/proc/net/pktgen/$netif
if [ ! -e $PGDEV ]; then
	echo "Error: pktgen might not be loaded or $netif is not up now!"
	exit
fi
count=1000000
pgset "count $count"
pgset "clone_skb 100000"
pgset "pkt_size $pkt_size"	# nic adds 4B CRC
pgset "delay $delay"
pgset "dst 10.0.0.2"
pgset "dst_mac 65:d1:0d:53:0f:00"

echo "$PGDEV is configured: pkt_size=$pkt_size w/ count=$count and delay=$delay"

PGDEV=/proc/net/pktgen/pgctrl
echo "Running..."
pgset "start"
echo "Done"

cat /proc/net/pktgen/$netif
