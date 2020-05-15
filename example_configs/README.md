# Example configurations for Anjay

Each of the subdirectories here contain a complete configuration that can be used when compiling Anjay without the use of CMake. They are intended as a starting point for custom configurations, but can also be used to compile Anjay without modifications.

You can use them by copying the contents of one of the directories to some location used as a include path by your compiler (e.g. `-I...` argument on most command-line compilers), or specify it as a include path directly (e.g. `-I$ANJAY_DIR/example_configs/linux_lwm2m10`, provided that Anjay has been downloaded into `$ANJAY_DIR`).

## LwM2M 1.0 configurations

These configurations can be used with both open source and commercial versions of Anjay.

* `linux_lwm2m10` - equivalent to the default configuration of CMake (i.e., running `cmake .`) on a typical modern desktop Linux system.
  * As-is, it should be usable on most Linux-based systems for 32- and 64-bit little-endian architectures, with GCC as the compiler.
  * Also likely to work on other Unix-like systems (*BSD, macOS, etc.) using GCC or Clang as the compiler with little to no modifications.
  * Depends on mbed TLS for security and PThreads for synchronization primitives.
* `embedded_lwm2m10` - starting configuration for embedded devices
  * Configured with any dependency on compiler extensions disabled - it should be compilable with a wide variety of compilers.
  * Various buffer sizes are reduced from the defaults to reduce resource consumption.
  * Depends on mbed TLS for security.
  * lwIP is used by default as the network stack.
  * Requires user-provided implementations for `avs_compat_threading`, as well as `avs_time_real_now()` and `avs_time_monotonic_now()` functions.

## LwM2M 1.1 configurations

**In the commercial version of Anjay ONLY**, there are additional `linux_lwm2m11` and `embedded_lwm2m11` directories. These are equivalent to the above, but with LwM2M 1.1 support and other commercial-only features.

* `linux_lwm2m11` is equivalent to the default confguration of CMake (i.e., running `cmake .`) on the commercial version.
* `embedded_lwm2m11` can be thought of as differences between `linux_lwm2m11` and `linux_lwm2m10` applied onto `embedded_lwm2m10`.
