# Anjay LwM2M library [<img align="right" height="50px" src="https://avsystem.github.io/Anjay-doc/_images/avsystem_logo.png">](http://www.avsystem.com/)

[![Build Status](https://travis-ci.org/AVSystem/Anjay.svg?branch=master)](https://travis-ci.org/AVSystem/Anjay)
[![Coverity Status](https://scan.coverity.com/projects/13206/badge.svg)](https://scan.coverity.com/projects/avsystem-anjay)

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
    * [Ubuntu 16.04 LTS](#ubuntu-1604-lts)
    * [CentOS 7](#centos-7)
    * [macOS Sierra with Homebrew](#macos-sierra-with-homebrewhttpsbrewsh)
    * [Windows](#windows)
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
    -   [pybind11](https://github.com/pybind/pybind11) - included in the repository as a subproject,
    -   [scan-build](https://clang-analyzer.llvm.org/scan-build.html) - for static analysis.

#### Ubuntu 16.04 LTS

``` sh
sudo apt-get install git build-essential cmake libmbedtls-dev zlib1g-dev
# Optionally for tests:
sudo apt-get install libpython3-dev libssl-dev python3 python3-cryptography python3-jinja2 python3-sphinx python3-requests clang valgrind clang-tools
```

#### CentOS 7

``` sh
# Required for mbedtls-devel and python3.5
sudo yum install -y https://centos7.iuscommunity.org/ius-release.rpm
sudo yum install -y which git make cmake mbedtls-devel gcc gcc-c++

# Optionally for tests:
sudo yum install -y valgrind valgrind-devel openssl openssl-devel python35u python35u-devel python35u-pip python-sphinx python-sphinx_rtd_theme clang-analyzer
# Some test scripts expect Python >=3.5 to be available via `python3` command
# Use update-alternatives to create a /usr/bin/python3 symlink with priority 0
# (lowest possible)
sudo update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.5 0
sudo python3 -m pip install cryptography jinja2 requests
```

#### macOS Sierra with [Homebrew](https://brew.sh/)

``` sh
brew install cmake mbedtls
# Optionally for tests:
brew install python3 openssl llvm
pip3 install cryptography sphinx sphinx_rtd_theme requests
```

#### Windows

Windows support is currently in a preliminary stage. See [README.Windows.md](README.Windows.md) for details.

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

By default demo client compiles with DTLS enabled and uses `mbedtls` as a DTLS provider,
but you may choose other DTLS backends currently supported by setting `DTLS_BACKEND` in
a CMake invocation to one of the following DTLS backends: `openssl`, `mbedtls` or `tinydtls`:

``` sh
cmake . -DDTLS_BACKEND="mbedtls" && make -j
```

Or, if a lack of security (not recommended) is what you need for some reason:

```sh
cmake . -DDTLS_BACKEND="" && make -j
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

Running tests on Ubuntu 16.04:
``` sh
./devconfig && make check
```

Running tests on CentOS 7:
``` sh
# NOTE: clang-3.4 static analyzer (default version for CentOS) gives false
# positives. --without-analysis flag disables static analysis.
./devconfig --without-analysis -DPython_ADDITIONAL_VERSIONS=3.5 && make check
```

Running tests on macOS Sierra:
``` sh
# If the scan-build script is located somewhere else, then you need to
# specify a different SCAN_BUILD_BINARY. Below, we are assumming scan-build
# comes from an llvm package, installed via homebrew.
./devconfig -DSCAN_BUILD_BINARY=/usr/local/Cellar/llvm/*/bin/scan-build && make check
```

## License

See [LICENSE](LICENSE) file.

### Commercial support

Anjay LwM2M library comes with the option of [full commercial support, provided by AVSystem](https://www.avsystem.com/products/anjay/).

## Contributing

Contributions are welcome! See our [contributing guide](CONTRIBUTING.rst).
