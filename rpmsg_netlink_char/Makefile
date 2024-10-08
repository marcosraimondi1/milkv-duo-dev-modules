obj-m := rpmsg_netlink_char.o

SRC := $(shell pwd)

all:
	make -C $(LINUXDIR) M=$(shell pwd)

clean:
	make -C $(LINUXDIR) M=$(shell pwd) clean
