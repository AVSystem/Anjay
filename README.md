# Anjay LwM2M library [<img align="right" height="50px" src="https://encrypted-tbn2.gstatic.com/images?q=tbn:ANd9GcSoiMy6rnzARUEdR0OjHmPGxTeiAMLBFlUYwIB9baWYWmuUwTbo">](http://www.avsystem.com/)

[![Build Status](https://travis-ci.org/AVSystem/Anjay.svg?branch=master)](https://travis-ci.org/AVSystem/Anjay)

## What is Anjay?

Anjay is a C library that aims to be the reference implementation of the OMA Lightweight Machine-to-Machine (LwM2M) device management protocol. It eases development of fully-featured LwM2M client applications by taking care of protocol details, allowing the user to focus on device-specific aspects.

The project has been created and is actively maintained by [AVSystem](https://www.avsystem.com).

-   [Full documentation](https://AVSystem.github.io/Anjay-doc/)
-   [Tutorials](https://AVSystem.github.io/Anjay-doc/BasicTutorial.html)
-   [API docs](https://AVSystem.github.io/Anjay-doc/api/)

<!-- toc -->

* [Supported features](#supported-features)
* [About OMA LwM2M](#about-oma-lwm2m)
* [Quickstart guide](#quickstart-guide)
  * [Dependencies](#dependencies)
  * [Running the demo client](#running-the-demo-client)
  * [Detailed compilation guide](#detailed-compilation-guide)
* [License](#license)
  * [Commercial support](#commercial-support)
* [Contributing](#contributing)

<!-- /toc -->

## Supported features

- LwM2M Bootstrap Interface:
    - Request
    - Finish
    - Write
    - Delete
    - Discover

- LwM2M Client Registration Interface:
    - Register
    - Update
    - De-register

- LwM2M Device Management and Service Enablement Interface:
    - Read
    - Discover
    - Write
    - Write-Attributes
    - Execute
    - Create
    - Delete

- LwM2M Information Reporting Interface:
    - Observe
    - Notify
    - Cancel Observation

- LwM2M Security modes:
    - DTLS with Certificates (if supported by backend TLS library)
    - DTLS with PSK (if supported by backend TLS library)
    - NoSec mode

- Supported TLS backends:
    - mbed TLS
    - OpenSSL
    - tinydtls

- CoAP data formats:
    - TLV
    - Opaque
    - Plain Text (including base64 encoding of opaque data)

- CoAP BLOCK transfers (for transferring data that does not fit in a single UDP packet):
    - Block1 (sending / receiving requests)
    - Block2 (sending responses)

- Pre-implemented LwM2M Objects:
    - Access Control
    - Security
    - Server

- Stream-oriented persistence API

## About OMA LwM2M

OMA LwM2M is a remote device management and telemetry protocol designed to conserve network resources. It is especially suitable for constrained wireless devices, where network communication is a major factor affecting battery life. LwM2M features secure (DTLS-encrypted) methods of remote bootstrapping, configuration and notifications over UDP or SMS.

More details about OMA LwM2M: [Brief introduction to LwM2M](https://AVSystem.github.io/Anjay-doc/LwM2M.html)

## Quickstart guide

### Dependencies

-   C compiler with C99 support,
-   [CMake 2.8.11+](https://cmake.org/),
-   [avs\_commons](https://github.com/AVSystem/avs_commons/) - included in the repository as a subproject,
-   If DTLS support is enabled, at least one of:
    -   [OpenSSL 1.1+](https://www.openssl.org/),
    -   [mbed TLS 2.0+](https://tls.mbed.org/),
    -   [tinydtls 0.9+](https://projects.eclipse.org/projects/iot.tinydtls),
-   Optional dependencies (required for tests):
    -   C++ compiler with C++11 support,
    -   [Python 3.5+](https://www.python.org/),
    -   [boost::python](https://www.boost.org/doc/libs/release/libs/python/).

To install everything on Ubuntu 16.04 LTS:

``` sh
sudo apt-get install git build-essential cmake libmbedtls-dev wget
# Optionally for tests:
sudo apt-get install libboost-python-dev libpython3-dev libssl-dev python3
```

Or on macOS Sierra with [Homebrew](https://brew.sh/):

``` sh
brew install cmake mbedtls wget
# Optionally for tests:
brew install python3 && brew install boost-python --with-python3
```

### Running the demo client

To compile Anjay demo client and connect it to a local LwM2M server listening on default 5683 port:

``` sh
git clone https://github.com/AVSystem/Anjay.git \
    && cd Anjay \
    && git submodule update --init \
    && cmake . \
    && make -j \
    && ./output/bin/demo --server-uri coap://127.0.0.1:5683
```

### Detailed compilation guide

First, make sure all necessary submodules are downloaded and up-to-date:

``` sh
git submodule update --init
```

To compile the library and demo application:

``` sh
cmake . && make -j
```

Compiled executables, including demo client, can be found in output/bin subdirectory.

For a detailed guide on configuring and compiling the project (including cross-compiling), see [Compiling client applications](https://AVSystem.github.io/Anjay-doc/Compiling_client_applications.html).

To start the demo client:

``` sh
# uses plain CoAP
./output/bin/demo --server-uri coap://127.0.0.1:5683

# uses DTLS in PSK mode, with PSK identity "foo" and secret key "bar" (hex-encoded)
./output/bin/demo --server-uri coaps://127.0.0.1:5684 --security-mode psk --identity 666f6f --key 626172
```

**NOTE**: When establishing a DTLS connection, the URI MUST use "coaps://". In NoSec mode (default), the URI MUST use "<coap://>".

Running tests:

``` sh
./devconfig && make check
```

## License

See [LICENSE](LICENSE) file.

### Commercial support

Anjay LwM2M library comes with the option of [full commercial support, provided by AVSystem](https://www.avsystem.com/products/anjay/).

## Contributing

Contributions are welcome! See our [contributing guide](CONTRIBUTING.rst).
