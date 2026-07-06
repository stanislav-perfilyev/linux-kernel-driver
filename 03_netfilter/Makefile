obj-m += netfilter_hook.o

KDIR  ?= /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	sudo insmod netfilter_hook.ko
	@echo "Check: cat /proc/netfilter_hook/stats"
	@echo "Add rule: echo '1.2.3.4:80' | sudo tee /proc/netfilter_hook/rules"

uninstall:
	sudo rmmod netfilter_hook || true

.PHONY: all clean install uninstall
