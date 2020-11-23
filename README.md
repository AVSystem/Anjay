# Anjay LwM2M library [<img align="right" height="50px" src="https://avsystem.github.io/Anjay-doc/_images/avsystem_logo.png">](http://www.avsystem.com/)

[![Build Status](https://travis-ci.org/AVSystem/Anjay.svg?branch=master)](https://travis-ci.org/AVSystem/Anjay)
[![Coverity Status](https://scan.coverity.com/projects/13206/badge.svg)](https://scan.coverity.com/projects/avsystem-anjay)

## What is Anjay?

Anjay is a C library that aims to be the reference implementation of the OMA Lightweight Machine-to-Machine (LwM2M) device management protocol. It eases development of fully-featured LwM2M client applications by taking care of protocol details, allowing the user to focus on device-specific aspects.

The project has been created and is actively maintained by [AVSystem](https://www.avsystem.com).

-   [Full documentation](https://AVSystem.github.io/Anjay-doc/)
-   [Tutorials](https://AVSystem.github.io/Anjay-doc/BasicClient.html)
-   [API docs](https://AVSystem.github.io/Anjay-doc/api/)

<!-- toc -->

* [Supported features](#supported-features)
* [About OMA LwM2M](#about-oma-lwm2m)
* [Quickstart guide](#quickstart-guide)
  * [Dependencies](#dependencies)
    * [Ubuntu 16.04 LTS / Raspbian Buster or later](#ubuntu-1604-lts--raspbian-buster-or-later)
    * [CentOS 7 or later](#centos-7-or-later)
    * [macOS Sierra or later, with Homebrew](#macos-sierra-or-later-with-homebrew)
  * [Running the demo client](#running-the-demo-client)
  * [Detailed compilation guide](#detailed-compilation-guide)
    * [Building using CMake](#building-using-cmake)
    * [Alternative build systems](#alternative-build-systems)
  * [Use a Dockerfile](#use-a-dockerfile)
* [Mbed OS port](#mbed-os-port)
* [Zephyr OS port](#zephyr-os-port)
* [License](#license)
  * [Commercial support](#commercial-support)
* [Contributing](#contributing)

<!-- /toc -->

## Supported features

This version includes full support for OMA LwM2M TS 1.0 features. Version that supports TS 1.1 is [available commercially](#commercial-support).

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

- Supported platforms:
    - any Unix-like operating system, such as Linux (including Android), macOS and BSD family
    - Microsoft Windows (preliminary support, see [README.Windows.md](README.Windows.md) for details)
    - any embedded platform (e.g. FreeRTOS, ThreadX) with lwIP networking stack
    - porting is possible for any other platform that has ISO C99 compiler available, see [Porting guide for non-POSIX platforms](https://avsystem.github.io/Anjay-doc/PortingGuideForNonPOSIXPlatforms.html) for details
        - preimplemented [integration layer for Arm Mbed OS](https://github.com/AVSystem/Anjay-mbedos) and an [example client based on it](https://github.com/AVSystem/Anjay-mbedos-client) are available
        - [example client](https://github.com/AVSystem/Anjay-zephyr-client) based on Zephyr OS is available

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
-   [avs\_commons](https://github.com/AVSystem/avs_commons/) - included in the repository as a subproject,
-   If DTLS support is enabled, at least one of:
    -   [OpenSSL 1.1+](https://www.openssl.org/),
    -   [mbed TLS 2.0+](https://tls.mbed.org/),
    -   [tinydtls 0.9+](https://projects.eclipse.org/projects/iot.tinydtls),
-   Optional dependencies (required for tests):
    -   [CMake 3.4+](https://cmake.org/) - non-mandatory, but preferred build system,
    -   C++ compiler with C++11 support,
    -   [Python 3.5+](https://www.python.org/),
    -   [pybind11](https://github.com/pybind/pybind11) - included in the repository as a subproject,
    -   [scan-build](https://clang-analyzer.llvm.org/scan-build.html) - for static analysis,
-   Optional dependencies (required for building documentation - more information in "Contributing" section):
    -   [Doxygen](http://www.doxygen.nl/),
    -   [Sphinx](https://www.sphinx-doc.org/en/master/).

#### Ubuntu 16.04 LTS / Raspbian Buster or later

<!-- deps_install_begin -->
``` sh
sudo apt-get install git build-essential cmake libmbedtls-dev zlib1g-dev
```
<!-- deps_install_end -->

#### CentOS 7 or later

``` sh
# EPEL is required for mbedtls-devel and cmake3
sudo yum install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
sudo yum install -y which git make cmake3 mbedtls-devel gcc gcc-c++ zlib-devel
```

#### macOS Sierra or later, with [Homebrew](https://brew.sh/)

``` sh
brew install cmake mbedtls
```

### Running the demo client

For initial development and testing of LwM2M clients, we recommend using the [Try Anjay platform](https://www.avsystem.com/try-anjay/) where you can use the basic LwM2M server functionality for free.

After setting up an account and adding the device entry, you can compile Anjay demo client and connect it to the platform by running:

<!-- compile_instruction_begin -->
``` sh
git clone https://github.com/AVSystem/Anjay.git \
    && cd Anjay \
    && git submodule update --init \
    && cmake . \
    && make -j \
    && ./output/bin/demo --endpoint-name $(hostname) --server-uri coap://try-anjay.avsystem.com:5683
```
<!-- compile_instruction_end -->

**NOTE**: On some older systems like CentOS 7, you may need to use `cmake3` instead of `cmake`.

**NOTE**: We strongly recommend replacing `$(hostname)` with some actual unique hostname. Please see the [documentation](https://avsystem.github.io/Anjay-doc/LwM2M.html#clients-and-servers) for information on preferred endpoint name formats. Note that with the Try Anjay platform, you will need to enter the endpoint name into the server UI first.

### Detailed compilation guide

First, make sure all necessary submodules are downloaded and up-to-date:

``` sh
git submodule update --init
```

After that, you have several options to compile the library.

#### Building using CMake

The preferred way of building Anjay is to use CMake.

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
./output/bin/demo --endpoint-name $(hostname) --server-uri coap://try-anjay.avsystem.com:5683

# uses DTLS in PSK mode, with PSK identity "foo" and secret key "bar" (hex-encoded)
./output/bin/demo --endpoint-name $(hostname) --server-uri coaps://try-anjay.avsystem.com:5684 --security-mode psk --identity 666f6f --key 626172
```

**NOTE**: When establishing a DTLS connection, the URI MUST use "coaps://". In NoSec mode (default), the URI MUST use "<coap://>".

#### Alternative build systems

Alternatively, you may use any other build system. You will need to:

* Prepare your `avs_commons_config.h`, `avs_coap_config.h` and `anjay_config.h` files.
  * Comments in [`avs_commons_config.h.in`](https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_commons_config.h.in), [`avs_coap_config.h.in`](deps/avs_coap/include_public/avsystem/coap/avs_coap_config.h.in) and [`anjay_config.h.in`](include_public/anjay/anjay_config.h.in) will guide you about the meaning of various settings.
  * You may use one of the directories from [`example_configs`](example_configs) as a starting point. See [`README.md`](example_configs/README.md) inside that directory for details. You may even set one of the subdirectories there as an include path directly in your compiler if you do not need any customizations.
* Configure your build system so that:
  * At least all `*.c` and `*.h` files from `src`, `include_public`, `deps/avs_coap/src`, `deps/avs_coap/include_public`, `deps/avs_commons/src` and `deps/avs_commons/include_public` directories are preserved, with the directory structure intact.
    * It is also safe to merge contents of all `include_public` directories into one. Merging `src` directories should be safe, too, but is not explicitly supported.
  * All `*.c` files inside `src`, `deps/avs_coap/src`, `deps/avs_commons/src`, or any of their direct or indirect subdirectories are compiled.
  * `deps/avs_commons/src` and `deps/avs_commons/include_public` directories are included in the header search path when compiling `avs_commons`.
  * `deps/avs_coap/src`, `deps/avs_coap/include_public` and `deps/avs_commons/include_public` directories are included in the header search path when compiling `avs_coap`.
  * `src`, `include_public`, `deps/avs_coap/include_public` and `deps/avs_commons/include_public` directories are included in the header search path when compiling Anjay.
  * `include_public`, `deps/avs_coap/include_public` and `deps/avs_commons/include_public` directories, or copies of them (possibly merged into one directory) are included in the header search path when compiling dependent application code.

Below is an example of a simplistic build process, that builds all of avs_commons, avs_coap and Anjay from a Unix-like shell:

```sh
# configuration
cp -r example_configs/linux_lwm2m10 config
# you may want to edit the files in the "config" directory before continuing

# compilation
cc -Iconfig -Iinclude_public -Ideps/avs_coap/include_public -Ideps/avs_commons/include_public -Isrc -Ideps/avs_coap/src -Ideps/avs_commons/src -c $(find src deps/avs_coap/src deps/avs_commons/src -name '*.c')
ar rcs libanjay.a *.o

# installation
cp libanjay.a /usr/local/lib/
cp -r include_public/avsystem /usr/local/include/
cp -r deps/avs_coap/include_public/avsystem /usr/local/include/
cp -r deps/avs_commons/include_public/avsystem /usr/local/include/
cp -r config/* /usr/local/include/
```

### Use a Dockerfile

For some cases you may find it comfortable to use Docker image. In this case, the only dependency is Docker, which you can install with your favorite package manager.
If Docker is already installed, you can clone the repo and build the Docker image:

```
git clone --recurse-submodules https://github.com/AVSystem/Anjay.git
cd Anjay
docker build --no-cache --tag anjay .
```

Then, you can launch the built image and run the demo client:

```
docker run -it anjay
./output/bin/demo -e $(hostname) -u coap://try-anjay.avsystem.io:5683
```


## Mbed OS port

If you want to use Anjay on Mbed OS, you might be interested in the [Anjay-mbedos](https://github.com/AVSystem/Anjay-mbedos) and [Anjay-mbedos-client](https://github.com/AVSystem/Anjay-mbedos-client) repositories, which contain basic integration with that system.

## Zephyr OS port

If you want to use Anjay on Zephyr OS, you might want to check our [example client](https://github.com/AVSystem/Anjay-zephyr-client) based on it.

## License

See [LICENSE](LICENSE) file.

### Commercial support

Anjay LwM2M library comes with the option of [full commercial support, provided by AVSystem](https://www.avsystem.com/products/anjay/).

The commercial version supports the latest LwM2M 1.1 release, including Composite operations, Send method, SenML JSON and CBOR data formats, TCP, SMS and NIDD bindings.

If you're interested in LwM2M Server, be sure to check out the [Coiote IoT Device Management](https://www.avsystem.com/products/coiote-iot-dm/) platform by AVSystem, which also focuses on LwM2M along with its latest 1.1 specification.

## Contributing

Contributions are welcome! See our [contributing guide](CONTRIBUTING.rst).

# Building documentation

Make sure, both Doxygen and Sphinx are installed on your system, then type:

``` sh
cmake . && make doc
```

the documentation will be available under `output/doc/doxygen` and `output/doc/sphinx`.
