InHouse-RDMA
============
Prototype that captures ethernet frames on virtex5 netfpga-10g

I started to develop a multi input fifo per port in order to increase the buffer size without incrementig the main buffer (because it doesn't meet timing if it's oversized).
There is 1 day of work to implement and maybe another day to debug here.
