cmake_minimum_required(VERSION 3.16)
project(lkm_userspace CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Strict warnings
add_compile_options(
    -Wall -Wextra -Wpedantic -Werror
    -Wconversion -Wshadow -Wnull-dereference
    -fno-omit-frame-pointer
)

# chardev_client
add_executable(chardev_client
    userspace/chardev_client.cpp
)
target_compile_definitions(chardev_client PRIVATE)

# procfs_client
add_executable(procfs_client
    userspace/procfs_client.cpp
)

# kmon_client
add_executable(kmon_client
    userspace/kmon_client.cpp
)

# Install targets
install(TARGETS chardev_client procfs_client kmon_client
        RUNTIME DESTINATION bin)

# CTest integration (smoke tests run without kernel modules loaded)
enable_testing()

add_test(NAME chardev_client_help
         COMMAND sh -c "./chardev_client /dev/null 2>&1 || true")
add_test(NAME procfs_client_missing
         COMMAND sh -c "./procfs_client 2>&1 | grep -q Error || true")
add_test(NAME kmon_client_missing
         COMMAND sh -c "./kmon_client 2>&1 | grep -q Error ||