
obj-m += oneUpPower.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD)/src modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD)/src clean

.PHONY: all clean
