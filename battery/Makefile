ifneq ($(KERNELRELEASE),)

obj-m += oneUpPower.o

else

KVERSION := $(shell uname -r)
KDIR := $(shell bash ./kdir.sh $(KVERSION))
PWD := $(shell pwd)
CC_VERSION_TEXT := $(shell gcc --version | head -n 1)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) "CONFIG_CC_VERSION_TEXT=$(CC_VERSION_TEXT)" modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) "CONFIG_CC_VERSION_TEXT=$(CC_VERSION_TEXT)" clean

endif

.PHONY: all clean
