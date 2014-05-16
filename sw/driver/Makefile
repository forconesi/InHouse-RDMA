obj-m += nf10.o
nf10-objs += ael2005_simple_conf.o
nf10-objs += nf10_main.o
nf10-objs += nf10_lbuf.o
nf10-objs += nf10_ethtool.o
nf10-objs += nf10_fops.o
nf10-objs += skbpool.o

ccflags-y += -g -DDEBUG

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
