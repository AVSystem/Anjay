# Anjay LwM2M library [<img align="right" height="50px" src="https://avsystem.github.io/Anjay-doc/_images/avsystem_logo.png">](http://www.avsystem.com/)

## ALPHA RELEASE NOTICE

This is an alpha, work-in-progress release of Anjay 4. All APIs are a subject to
change without any notice.

### Currently supported features

- no dynamic memory allocation
- UDP binding
- LwM2M 1.1 & 1.2
- TLV Content-Format for input operations
- CBOR and SenML CBOR Content-Format for output operations
- Static data model supporting all operations, except Composite operations
- LwM2M Send support
- Minimal implementations of Security (/0), Server (/1) and Device (/3) objects
- Full implementation for Firmware Update (/5) object
- Partial support for LwM2M Notify (only on resource level, with `pmin` and
  `pmax` attributes)

## What is Anjay?

Anjay is a C library that aims to be the reference implementation of the OMA
Lightweight Machine-to-Machine (LwM2M) device management protocol. It eases
development of fully-featured LwM2M client applications by taking care of
protocol details, allowing the user to focus on device-specific aspects.

The project has been created and is actively maintained by
[AVSystem](https://www.avsystem.com).

## Getting started

> **__NOTE:__** Currently, the only supported OS that can run examples and tests
> is Linux.

### Building

```sh
mkdir build
cd build
cmake ..

make -j all
```

### Running examples

There are 3 examples available:
- `anjay_basic_example`, which is a minimal application that instantiates a
  simulated temperature sensor
- `anjay_send_example`, which additionally periodicaly reports temperature
  readings using LwM2M Send method
- `anjay_firmware_update_example`, which uses LwM2M Firmware Update object to
  download and run the downloaded binary

```sh
cd build
./anjay_basic_example endpoint_name
./anjay_send_example endpoint_name
./anjay_firmware_update_example endpoint_name
```

### Running tests

```sh
cd build
./dm_tests
./fluf_tests
./sdm_tests
```

Additionally, these tests can be run with Valgrind:

```sh
cd build
make fluf_tests_with_valgrind
make dm_tests_with_valgrind
make sdm_tests_with_valgrind
```
