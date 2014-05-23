obj-m += nf10.o
nf10-objs += ael2005_simple_conf.o
nf10-objs += nf10_main.o
nf10-objs += nf10_lbuf.o
nf10-objs += nf10_ethtool.o
nf10-objs += nf10_fops.o
ifeq ($(CONFIG_XEN_NF_BACKEND),y)
nf10-objs += xen_nfback.o
endif
ifeq ($(CONFIG_SKBPOOL),y)
nf10-objs += skbpool.o
endif

ccflags-y += -g -DDEBUG
ccflags-$(CONFIG_XEN_NF_BACKEND) += -DCONFIG_XEN_NF_BACKEND
ccflags-$(CONFIG_SKBPOOL) += -DCONFIG_SKBPOOL
ccflags-$(CONFIG_PROFILE) += -DCONFIG_PROFILE

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
