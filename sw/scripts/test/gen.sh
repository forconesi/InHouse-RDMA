#!/bin/sh
nr_pkt=100
if [ $# -eq 1 ]; then
	nr_pkt=$1
fi

#tcpreplay -t -i nf0 /root/InHouse-RDMA/pcap_input_files/${nr_pkt}_pkts-iperf
tcpreplay -p 1 -i nf0 /root/InHouse-RDMA/pcap_input_files/${nr_pkt}_pkts-iperf
