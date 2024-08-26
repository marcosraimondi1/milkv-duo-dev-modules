# SPDX-License-Identifier: GPL-2.0-only
obj-m := rpmsg.o

all:
	make -C $(LINUXDIR) M=$(shell pwd)

clean:
	make -C $(LINUXDIR) M=$(shell pwd) clean
