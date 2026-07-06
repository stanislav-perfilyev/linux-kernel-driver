obj-m += kthread_monitor.o

KDIR  ?= /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	sudo insmod kthread_monitor.ko poll_interval_ms=2000
	@echo "Watch: watch -n2 cat /proc/kmon/snapshot"
	@echo "Control: echo 'interval=500' | sudo tee /proc/kmon/control"

uninstall:
	sudo rmmod kthread_monitor || true

.PHONY: all clean install uninstall
