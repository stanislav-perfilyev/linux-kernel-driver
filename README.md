# Linux Kernel Driver (LKM)

> Four production-quality Linux loadable kernel modules with C++ userspace clients

[![CI](https://github.com/stanislav-perfilyev/linux-kernel-driver/actions/workflows/ci.yml/badge.svg)](https://github.com/stanislav-perfilyev/linux-kernel-driver/actions)
![Kernel](https://img.shields.io/badge/Linux_Kernel-5.15%2B-blue)
![License](https://img.shields.io/badge/license-GPL--2.0-orange)
![Language](https://img.shields.io/badge/C-kernel%20modules-lightgrey)
![Language](https://img.shields.io/badge/C%2B%2B17-userspace-blue)

## Overview

Four Linux Loadable Kernel Modules (LKM) covering the core areas of kernel-space programming:

| Module | Subsystem | Key APIs |
|--------|-----------|----------|
| `01_chardev` | Character device driver | `alloc_chrdev_region`, `cdev_add`, `file_operations`, `ioctl` |
| `02_procfs` | `/proc` filesystem driver | `proc_ops`, `seq_file`, `single_open` |
| `03_netfilter` | Netfilter hook (packet filter) | `nf_register_net_hook`, `NF_INET_PRE_ROUTING` |
| `04_kthread` | Kernel thread + workqueue | `kthread_run`, `alloc_ordered_workqueue`, `si_meminfo` |

Each module has a matching C++17 userspace client (`userspace/`) and a bash integration test (`tests/`).

## Repository Layout

```
linux-kernel-driver/
├── 01_chardev/          # Ring-buffer char device with ioctl
│   ├── chardev.h        # Shared kernel/userspace header (ioctl macros + stats struct)
│   └── chardev.c        # Module: ring buf + spinlock + wait_queue + poll
├── 02_procfs/           # /proc/mydriver/{stats,config} read/write
│   └── procfs_driver.c
├── 03_netfilter/        # IPv4 packet filter with IP/port blocklist
│   └── netfilter_hook.c # NF_INET_PRE_ROUTING hook + /proc rules interface
├── 04_kthread/          # Memory snapshot collector via kthread + workqueue
│   └── kthread_monitor.c
├── userspace/           # C++17 RAII clients + smoke tests
│   ├── chardev_client.cpp
│   ├── procfs_client.cpp
│   └── kmon_client.cpp
├── tests/               # Bash integration tests (require loaded modules)
│   ├── test_chardev.sh
│   ├── test_procfs.sh
│   ├── test_netfilter.sh
│   └── test_kthread.sh
├── CMakeLists.txt       # Userspace build (C++17, -Wall -Wextra -Wpedantic)
├── Makefile             # Kernel build + load/unload helpers
└── .github/workflows/ci.yml
```

## Module Details

### 01_chardev — Character Device Driver

A ring-buffer character device (`/dev/chardev`) demonstrating the full kernel driver lifecycle.

**Features:**
- Power-of-2 ring buffer (configurable via `ring_size` module param)
- `spinlock_t` for SMP-safe concurrent access
- `wait_queue_head_t` for blocking `read()` (woken by `write()`)
- `poll()` support (`POLLIN` when data available)
- Three ioctl commands: `GET_STATS`, `RESET`, `SET_FILTER_PID`
- Proper error-path unwinding with `goto` labels

### 02_procfs — /proc Filesystem Driver

Exposes driver state via two `/proc` entries using the modern `proc_ops` + `seq_file` API.

| Entry | Access | Content |
|-------|--------|---------|
| `/proc/mydriver/stats` | read-only | uptime, msg_count, last_pid, log_level, max_msgs |
| `/proc/mydriver/config` | read/write | `log_level=N`, `max_msgs=N` |

### 03_netfilter — Packet Filter Hook

Installs an `NF_INET_PRE_ROUTING` Netfilter hook to block IPv4 packets by IP/port rules.

**Features:**
- Block rules stored in a kernel linked list (`list_head` + `kzalloc`)
- Rule management via `/proc/netfilter/{stats,rules}`
- Handles TCP and UDP (`tcp_hdr` / `udp_hdr`)
- `atomic64_t` counters for passed/dropped/errors

### 04_kthread — Memory Monitor

A kernel thread that periodically collects system memory snapshots via `/proc`.

**Features:**
- `kthread_run` + `alloc_ordered_workqueue` pattern
- `si_meminfo()` for RAM/swap statistics
- Poll-able `/proc/kmon/snapshot` via `atomic_t` + `wake_up_interruptible`
- `/proc/kmon/control`: `stop`, `start`, `interval=N` (100–60000 ms)

## Building

### Requirements

```bash
sudo apt install linux-headers-$(uname -r) build-essential cmake
```

### Kernel modules

```bash
make modules          # build all 4 .ko files
sudo make load        # insmod all
sudo make unload      # rmmod all
make clean
```

### Userspace clients

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

## CI/CD

| Job | What it does |
|-----|--------------|
| `build-userspace` | C++ clients + CTest on ubuntu-22.04 |
| `syntax-check-kernel` | Installs kernel headers, Kbuild syntax validation |

## Integration Tests

```bash
sudo make load
bash tests/test_chardev.sh
bash tests/test_procfs.sh
bash tests/test_netfilter.sh
bash tests/test_kthread.sh
```

## License

GPL-2.0 — required for loadable kernel modules.

---

*Part of the [job-search-automation](https://github.com/stanislav-perfilyev/job-search-automation) portfolio pipeline.*
