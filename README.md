InHouse-RDMA
============
Prototype that captures ethernet frames on virtex5 netfpga-10g

Since the pcie endpoint signals not ready from time to time, it is necessary to have a larger internal buffer in the FPGA. The problem is that it takes a lot of time to implement the hardware and it does not meet timing easily. Another buffer scheme must be followed.
