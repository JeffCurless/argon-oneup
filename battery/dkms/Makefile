obj-m += oneUpPower.o

KVERSION := $(if $(DKMS_KERNEL_VERSION),$(DKMS_KERNEL_VERSION),$(shell uname -r))
KDIR := $(shell bash ./src/kdir.sh $(KVERSION))
PWD := $(shell pwd)
CC_VERSION_TEXT := $(shell gcc --version | head -n 1)

all:
	$(MAKE) -C $(KDIR) M=$(PWD)/src "CONFIG_CC_VERSION_TEXT=$(CC_VERSION_TEXT)" modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD)/src "CONFIG_CC_VERSION_TEXT=$(CC_VERSION_TEXT)" clean

.PHONY: all clean
