# Anjay LwM2M library [<img align="right" height="50px" src="https://avsystem.github.io/Anjay-doc/_images/avsystem_logo.png">](http://www.avsystem.com/)

## ALPHA RELEASE NOTICE

This is an alpha, work-in-progress release of Anjay 4. All APIs are subject to
change without any notice.

### Currently supported features

- no dynamic memory allocation
- UDP binding
- LwM2M 1.1 & 1.2
- TLV, PlainText, Opaque, CBOR, SenML CBOR, and LwM2M CBOR Content-Formats for input operations
- PlainText, Opaque, CBOR, SenML CBOR, and LwM2M CBOR Content-Formats for output operations
- Static data model supporting all operations, except Composite operations
- LwM2M Send support
- Full implementation for Security (/0), Server (/1), Device (/3), and Firmware Update (/5) objects

## What is Anjay?

Anjay is a C library that aims to be the reference implementation of the OMA
Lightweight Machine-to-Machine (LwM2M) device management protocol. It eases
the development of fully-featured LwM2M client applications by taking care of
protocol details, allowing the user to focus on device-specific aspects.

The project has been created and is actively maintained by
[AVSystem](https://www.avsystem.com).

## Getting started

> **__NOTE:__** Currently, the only supported OS that can run examples and tests
> is Linux.

### Building examples and tests

```sh
mkdir build
cd build/
cmake ..
make -j
```

Root `CMakeLists.txt` only groups individual examples and test CMake projects.
They can also be built separately in their own trees, e.g.:

```sh
cd examples/minimal_client
mkdir build
cd build/
cmake ..
make -j
```

### Running examples

Assuming that the commands above were used:

```sh
minimal_client_example/minimal_client_example endpoint_name
firmware_update_example/firmware_update_example endpoint_name
send_example/send_example endpoint_name
bootstrap_example/bootstrap_example endpoint_name
```

### Running tests

Directly:

```sh
dm_tests/dm_tests
fluf_tests/fluf_tests
sdm_tests/sdm_tests
```

With valgrind:

```sh
make dm_tests_with_valgrind
make fluf_tests_with_valgrind
make sdm_tests_with_valgrind
```

## PlatformIO library

Anjay supports PlatformIO integration. For more information, see [PlatformIO
README](platformio/README.md).
