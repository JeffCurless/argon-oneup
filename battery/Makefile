
# In-tree build (when CONFIG_BATTERY_ONEUP is set by Kconfig)
obj-$(CONFIG_BATTERY_ONEUP) += oneUpPower.o

# Out-of-tree / DKMS build: fall back to obj-m when building outside the
# kernel source tree (i.e. CONFIG_BATTERY_ONEUP is not set by Kconfig).
obj-m += oneUpPower.o

KVERSION := $(shell uname -r)
KDIR := /lib/modules/$(KVERSION)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
