/**
 * kmon_client.cpp — C++ client for /proc/kmon/{snapshot,control}
 *
 * Reads memory snapshot, sets interval, polls for updates.
 *
 * Build: g++ -std=c++17 -Wall -Wextra -o kmon_client kmon_client.cpp
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <cerrno>
#include <unistd.h>

static std::string read_proc(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::system_error(errno, std::generic_category(),
                                "open " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void write_proc(const std::string &path, const std::string &val) {
    std::ofstream f(path);
    if (!f.is_open())
        throw std::system_error(errno, std::generic_category(),
                                "open " + path);
    f << val;
}

int main() {
    try {
        printf("[Test 1] Read /proc/kmon/snapshot\n");
        auto snap = read_proc("/proc/kmon/snapshot");
        printf("%s\n", snap.c_str());

        printf("[Test 2] Set interval=500ms\n");
        write_proc("/proc/kmon/control", "interval=500\n");
        sleep(1);
        snap = read_proc("/proc/kmon/snapshot");
        printf("After 500ms interval:\n%s\n", snap.c_str());

        printf("[Test 3] Stop + start\n");
        write_proc("/proc/kmon/control", "stop\n");
        printf("  Stopped\n");
        sleep(1);
        write_proc("/proc/kmon/control", "start\n");
        printf("  Started\n");

        /* Restore */
        write_proc("/proc/kmon/control", "interval=1000\n");
        printf("\n[+] All tests passed\n");
        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "[-] Error: %s\n  (Is kthread_monitor.ko loaded?)\n",
                e.what());
        return 1;
    }
}
