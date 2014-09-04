#!/bin/sh
rmmod nf10 2> /dev/null
rmmod xen_netback 2> /dev/null
insmod nf10.ko
ifconfig nf0 up
ifconfig eth1 up promisc
ifconfig nf0
ethtool -s nf0 msglvl 0xffff
ethtool nf0
