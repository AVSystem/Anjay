# Anjay on Windows

## Limitations

Anjay is currently not supported or regularly tested (which means that builds may break between releases) on Windows. However, there is preliminary support for building and running on Windows - with some limitations:

- Any degree of testing was only performed on Windows 10; the code is theoretically compatible with Windows Vista and up
- Building using MinGW from MSYS shell is currently the only supported toolchain
- Building as a shared library (DLL) is not currently supported
- Demo application has limited functionality - internal command line and IP Ping object are not available
- Testing frameworks are not supported
- SMS binding is not supported (applicable to commercial version only)

## Prerequisites

### Installing dependencies

1. Install [MSYS2](http://www.msys2.org/)
2. Install [Git for Windows](https://gitforwindows.org/)

   **NOTE:** You can also install these using [Chocolatey](https://chocolatey.org/): `choco install git msys2` but please make sure to still follow the instructions to update MSYS2 after installing it.

3. Open the appropriate MINGW shell (e.g., `C:\msys64\mingw32.exe` or `C:\msys64\mingw64.exe`, depending on whether you want to build 32- or 64-bit binaries) and install the compile-time dependencies:

   ``` sh
   pacman -Sy make ${MINGW_PACKAGE_PREFIX}-gcc ${MINGW_PACKAGE_PREFIX}-cmake ${MINGW_PACKAGE_PREFIX}-mbedtls
   ```

## Cloning the repository

Run the following command in a directory of choice **in the Git for Windows bash environment**:

``` sh
git clone --recurse-submodules https://github.com/AVSystem/Anjay.git
```

## Compiling the project

Run the following commands **in the MINGW shell**, after navigating to the directory created using Git above:

``` sh
cmake -G"MSYS Makefiles" .
make
```

## Running the demo application

The demo application can be run from the MINGW shell just like on any other Unix system, e.g.:

```
./output/bin/demo --endpoint-name $(hostname) --server-uri coap://try-anjay.avsystem.com:5683
```

If you want to run the resulting application outside of the MINGW shell, you will likely need to copy the DLL dependencies, such as:

* `libgcc_s_dw2-1.dll`
* `libmbedcrypto.dll`
* `libmbedtls.dll`
* `libmbedx509.dll`
* `libwinpthread-1.dll`
