cmake_minimum_required(VERSION 3.1)
add_subdirectory(minimal)
add_subdirectory(remote-host-port)
add_subdirectory(bind)
add_subdirectory(shutdown-remote-hostname)
add_subdirectory(stats)

# NOTE: ip-stickiness requires -DWITHOUT_IP_STICKINESS=OFF
# so it's not included here
