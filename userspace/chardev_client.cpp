/**
 * chardev_client.cpp — C++ userspace client for /dev/mymonitor
 *
 * Demonstrates:
 *  - Open/close character device
 *  - Write data, read it back
 *  - ioctl GET_STATS, RESET, SET_FILTER_PID
 *  - poll() for data availability
 *
 * Build: g++ -std=c++17 -Wall -Wextra -o chardev_client chardev_client.cpp
 * Run:   ./chardev_client [device_path]
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <string>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/types.h>

/* Shared kernel/userspace header (copy of 01_chardev/chardev.h) */
#include <linux/ioctl.h>

#define MYMONITOR_MAGIC  'M'
#define MYMONITOR_MAXMSG 256

struct mymonitor_stats {
    unsigned long reads;
    unsigned long writes;
    unsigned long bytes_written;
    unsigned long bytes_read;
    unsigned long ring_used;
    unsigned long ring_capacity;
};

#define IOCTL_GET_STATS    _IOR(MYMONITOR_MAGIC, 1, struct mymonitor_stats)
#define IOCTL_RESET        _IO(MYMONITOR_MAGIC,  2)
#define IOCTL_SET_FILTER_PID _IOW(MYMONITOR_MAGIC, 3, pid_t)

/* ------------------------------------------------------------------ */
class MonitorDevice {
public:
    explicit MonitorDevice(const char *path) {
        fd_ = open(path, O_RDWR | O_CLOEXEC);
        if (fd_ < 0)
            throw std::system_error(errno, std::generic_category(),
                                    std::string("open ") + path);
        path_ = path;
        printf("[+] Opened %s (fd=%d)\n", path_.c_str(), fd_);
    }

    ~MonitorDevice() {
        if (fd_ >= 0) {
            close(fd_);
            printf("[+] Closed %s\n", path_.c_str());
        }
    }

    /* Non-copyable */
    MonitorDevice(const MonitorDevice &) = delete;
    MonitorDevice &operator=(const MonitorDevice &) = delete;

    ssize_t write(const std::string &msg) {
        ssize_t n = ::write(fd_, msg.data(), msg.size());
        if (n < 0)
            throw std::system_error(errno, std::generic_category(), "write");
        return n;
    }

    std::string read(size_t max_bytes = 256) {
        std::string buf(max_bytes, '\0');
        ssize_t n = ::read(fd_, buf.data(), max_bytes);
        if (n < 0)
            throw std::system_error(errno, std::generic_category(), "read");
        buf.resize(static_cast<size_t>(n));
        return buf;
    }

    mymonitor_stats get_stats() {
        mymonitor_stats s{};
        if (ioctl(fd_, IOCTL_GET_STATS, &s) < 0)
            throw std::system_error(errno, std::generic_category(),
                                    "ioctl GET_STATS");
        return s;
    }

    void reset() {
        if (ioctl(fd_, IOCTL_RESET) < 0)
            throw std::system_error(errno, std::generic_category(),
                                    "ioctl RESET");
    }

    void set_filter_pid(pid_t pid) {
        if (ioctl(fd_, IOCTL_SET_FILTER_PID, &pid) < 0)
            throw std::system_error(errno, std::generic_category(),
                                    "ioctl SET_FILTER_PID");
    }

    /* Returns true if data available within timeout_ms */
    bool poll_readable(int timeout_ms = 1000) {
        struct pollfd pfd = { fd_, POLLIN, 0 };
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret < 0)
            throw std::system_error(errno, std::generic_category(), "poll");
        return (ret > 0) && (pfd.revents & POLLIN);
    }

private:
    int         fd_ = -1;
    std::string path_;
};

static void print_stats(const mymonitor_stats &s)
{
    printf("  reads         : %lu\n",  s.reads);
    printf("  writes        : %lu\n",  s.writes);
    printf("  bytes_written : %lu\n",  s.bytes_written);
    printf("  bytes_read    : %lu\n",  s.bytes_read);
    printf("  ring_used     : %lu\n",  s.ring_used);
    printf("  ring_capacity : %lu\n",  s.ring_capacity);
}

int main(int argc, char *argv[])
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/mymonitor";

    try {
        MonitorDevice dev_obj(dev);

        /* --- Test 1: write + read back --- */
        printf("\n[Test 1] Write / read\n");
        const std::string msg = "Hello from userspace pid=" +
                                std::to_string(getpid()) + "\n";
        ssize_t written = dev_obj.write(msg);
        printf("  Written %zd bytes\n", written);

        if (dev_obj.poll_readable(500)) {
            auto got = dev_obj.read();
            printf("  Read back: [%s]\n", got.c_str());
            assert(got == msg && "round-trip mismatch");
            printf("  Round-trip: OK\n");
        } else {
            printf("  No data after write (unexpected)\n");
        }

        /* --- Test 2: stats --- */
        printf("\n[Test 2] GET_STATS\n");
        auto s = dev_obj.get_stats();
        print_stats(s);
        assert(s.writes >= 1);
        assert(s.reads  >= 1);

        /* --- Test 3: reset --- */
        printf("\n[Test 3] RESET\n");
        dev_obj.reset();
        s = dev_obj.get_stats();
        assert(s.reads == 0 && s.writes == 0);
        printf("  Stats reset: OK\n");

        /* --- Test 4: SET_FILTER_PID --- */
        printf("\n[Test 4] SET_FILTER_PID (0 = no filter)\n");
        dev_obj.set_filter_pid(0);
        printf("  Filter cleared: OK\n");

        printf("\n[+] All tests passed\n");
        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "[-] Error: %s\n", e.what());
        return 1;
    }
}
