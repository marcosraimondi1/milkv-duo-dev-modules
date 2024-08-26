obj-m := rpmsg_netlink.o

SRC := $(shell pwd)

all:
	make -C $(LINUXDIR) M=$(shell pwd)

clean:
	make -C $(LINUXDIR) M=$(shell pwd) clean
