# Root Makefile — builds all kernel modules and userspace clients
# Usage:
#   make modules       — build all .ko files
#   make userspace     — build C++ clients (requires cmake)
#   make all           — both
#   make load          — insmod all modules (requires root)
#   make unload        — rmmod all modules (requires root)
#   make clean         — clean everything

SUBDIRS = 01_chardev 02_procfs 03_netfilter 04_kthread
BUILD_DIR = build

.PHONY: all modules userspace load unload clean test

all: modules userspace

modules:
	@for d in $(SUBDIRS); do \
		echo "==> Building $$d"; \
		$(MAKE) -C $$d all; \
	done

userspace:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@$(MAKE) -C $(BUILD_DIR) --no-print-directory

load:
	@echo "Loading kernel modules (requires root)..."
	sudo insmod 01_chardev/chardev.ko ring_size=8192 || true
	sudo insmod 02_procfs/procfs_driver.ko || true
	sudo insmod 03_netfilter/netfilter_hook.ko || true
	sudo insmod 04_kthread/kthread_monitor.ko poll_interval_ms=2000 || true
	@echo "Loaded modules:"
	@lsmod | grep -E "chardev|procfs_driver|netfilter_hook|kthread_monitor" || echo "(none)"

unload:
	@echo "Removing kernel modules..."
	-sudo rmmod kthread_monitor 2>/dev/null
	-sudo rmmod netfilter_hook  2>/dev/null
	-sudo rmmod procfs_driver   2>/dev/null
	-sudo rmmod chardev         2>/dev/null
	@echo "Unloaded."

test: userspace
	@echo "Running userspace smoke tests..."
	@cd $(BUILD_DIR) && ctest --output-on-failure

clean:
	@for d in $(SUBDIRS); do \
		$(MAKE) -C $$d clean 2>/dev/null; \
	done
	@rm -rf $(BUILD_DIR)
	@echo "Cleaned."
