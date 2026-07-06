obj-m += procfs_driver.o

KDIR  ?= /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	sudo insmod procfs_driver.ko
	@echo "Check: cat /proc/mydriver/stats"

uninstall:
	sudo rmmod procfs_driver || true

.PHONY: all clean install uninstall
