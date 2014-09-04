#!/bin/sh
[ "$arg" != "" ] || arg="-t"
[ "$trace_file" != "" ] || trace_file=../../../pcap_input_files/1000_pkts-iperf
[ -e "$trace_file" ] || trace_file=../../pcap_input_files/1000_pkts-iperf
if [ ! -e "$trace_file" ]; then
	echo "trace file($trace_file) is not found. specify 'trace_file' env var"
	exit
fi

if [ $# -ne 2 ]; then
	echo "Usage: $0 <netif> <# of packet of iperf trace (<=1000)>"
	exit
fi
netif=$1
nr_pkt=$2

tcpreplay $arg -i $netif -L $nr_pkt $trace_file
