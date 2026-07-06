/**
 * procfs_client.cpp — C++ client for /proc/mydriver/{stats,config}
 *
 * Reads stats, updates config, verifies the change.
 *
 * Build: g++ -std=c++17 -Wall -Wextra -o procfs_client procfs_client.cpp
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <cerrno>

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
    if (!f.good())
        throw std::runtime_error("write failed to " + path);
}

int main() {
    try {
        /* Read stats */
        printf("[Test 1] /proc/mydriver/stats\n");
        auto stats = read_proc("/proc/mydriver/stats");
        printf("%s\n", stats.c_str());

        /* Read config */
        printf("[Test 2] /proc/mydriver/config (current)\n");
        auto cfg = read_proc("/proc/mydriver/config");
        printf("%s\n", cfg.c_str());

        /* Update config */
        printf("[Test 3] Set log_level=2\n");
        write_proc("/proc/mydriver/config", "log_level=2\n");
        cfg = read_proc("/proc/mydriver/config");
        if (cfg.find("log_level=2") == std::string::npos)
            throw std::runtime_error("log_level not updated");
        printf("  Verified log_level=2: OK\n");

        printf("[Test 4] Set max_msgs=500\n");
        write_proc("/proc/mydriver/config", "max_msgs=500\n");
        cfg = read_proc("/proc/mydriver/config");
        if (cfg.find("max_msgs=500") == std::string::npos)
            throw std::runtime_error("max_msgs not updated");
        printf("  Verified max_msgs=500: OK\n");

        /* Restore */
        write_proc("/proc/mydriver/config", "log_level=1\n");
        write_proc("/proc/mydriver/config", "max_msgs=1000\n");
        printf("\n[+] All tests passed\n");
        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "[-] Error: %s\n  (Is procfs_driver.ko loaded?)\n",
                e.what());
        return 1;
    }
}
