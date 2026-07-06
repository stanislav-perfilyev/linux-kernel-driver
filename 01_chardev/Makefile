# Kbuild Makefile for chardev.ko
obj-m += chardev.o

KDIR  ?= /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	sudo insmod chardev.ko ring_size=8192 || true
	@if [ ! -c /dev/mymonitor ]; then \
		MAJOR=$$(cat /proc/devices | awk '/mymonitor/{print $$1}'); \
		sudo mknod /dev/mymonitor c $$MAJOR 0; \
		sudo chmod 666 /dev/mymonitor; \
	fi
	@echo "Installed: /dev/mymonitor"

uninstall:
	sudo rmmod chardev || true
	sudo rm -f /dev/mymonitor
	@echo "Removed"

.PHONY: all clean install uninstall
