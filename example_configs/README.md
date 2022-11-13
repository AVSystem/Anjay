# Example configurations for Anjay

Each of the subdirectories here contains a complete configuration that can be used when compiling Anjay without the use of CMake. They are intended as a starting point for custom configurations, but can also be used to compile Anjay without modifications.

You can use them by copying the contents of one of the directories to some location used as an include path by your compiler (e.g. `-I...` argument on most command-line compilers), or specify it as an include path directly (e.g. `-I$ANJAY_DIR/example_configs/linux_lwm2m11`, provided that Anjay has been downloaded into `$ANJAY_DIR`).

## LwM2M 1.1 configurations

* `linux_lwm2m11` - equivalent to the default configuration of CMake (i.e., running `cmake .`) on a typical modern desktop Linux system.
  * As-is, it should be usable on most Linux-based systems for 32- and 64-bit little-endian architectures, with GCC as the compiler.
  * Also likely to work on other Unix-like systems (*BSD, macOS, etc.) using GCC or Clang as the compiler with little to no modifications.
  * Depends on Mbed TLS for security and PThreads for synchronization primitives.
* `embedded_lwm2m11` - starting configuration for embedded devices
  * Configured with any dependency on compiler extensions disabled - it should be compilable with a wide variety of compilers.
  * Various buffer sizes are reduced from the defaults to reduce resource consumption.
  * Depends on Mbed TLS for security.
  * lwIP is used by default as the network stack.
  * Requires user-provided implementations for `avs_compat_threading`, as well as `avs_time_real_now()` and `avs_time_monotonic_now()` functions.

## LwM2M 1.0 configurations

* `linux_lwm2m10` - equivalent to `linux_lwm2m11` configuration, but with LwM2M 1.1-specific features disabled.
* `embedded_lwm2m10` - equivalent to `embedded_lwm2m11` configuration, but with LwM2M 1.1-specific features disabled.

