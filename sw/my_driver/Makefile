
obj-m += myd.o
myd-objs += ael2005_simple_conf.o
myd-objs += my_driver.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	#/root/Downloads/linux-3.14.2/scripts/sign-file sha512 /root/Downloads/linux-3.14.2/signing_key.priv /root/Downloads/linux-3.14.2/signing_key.x509 myd.ko 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
