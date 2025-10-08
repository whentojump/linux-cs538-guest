obj-m += fake_alloc.o

all:
	make -C $(shell realpath ..) M=$(shell realpath .) modules

clean:
	make -C $(shell realpath ..) M=$(shell realpath .) clean

.PHONY: all clean
