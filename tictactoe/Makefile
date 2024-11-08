obj-m := tictactoe_mod.o

SRC := $(shell pwd)

all:
	make -C $(LINUXDIR) M=$(shell pwd)

clean:
	make -C $(LINUXDIR) M=$(shell pwd) clean
