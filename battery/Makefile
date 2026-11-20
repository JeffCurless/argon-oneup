ifneq ($(KERNELRELEASE),)

# In-tree build (when CONFIG_BATTERY_ONEUP is set by Kconfig)
obj-$(CONFIG_BATTERY_ONEUP) += oneUpPower.o

# Out-of-tree / DKMS build: fall back to obj-m when building outside the
# kernel source tree (i.e. CONFIG_BATTERY_ONEUP is not set by Kconfig).
obj-m += oneUpPower.o

else

KVERSION := $(shell uname -r)
KDIR := $(shell bash ./kdir.sh $(KVERSION) 2>/dev/null)
PWD := $(shell pwd)
CC_VERSION_TEXT := $(shell gcc --version | head -n 1)

# `all` must be the first target so a bare `make` builds the module.
all: check-headers
	$(MAKE) -C $(shell bash ./kdir.sh $(KVERSION)) M=$(PWD) "CONFIG_CC_VERSION_TEXT=$(CC_VERSION_TEXT)" modules

check-headers:
	@if [ -z "$(KDIR)" ]; then \
		echo "Linux headers for $(KVERSION) not found. Attempting to install..."; \
		sudo apt install -y linux-headers-$(KVERSION); \
	fi

clean:
	$(MAKE) -C $(shell bash ./kdir.sh $(KVERSION)) M=$(PWD) "CONFIG_CC_VERSION_TEXT=$(CC_VERSION_TEXT)" clean

endif

.PHONY: all clean check-headers
