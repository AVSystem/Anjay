# Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.


FROM ubuntu:22.04
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive \
    apt-get install -yq git build-essential cmake zlib1g-dev doxygen python3 \
        libpython3-dev libssl-dev python3-pip clang-tools valgrind opensc \
        libengine-pkcs11-openssl docker.io nodejs curl jq automake
COPY requirements.txt requirements.txt
RUN pip3 install -r requirements.txt

RUN $(which echo) -e "                                                      \n\
                                                                            \n\
    set -e                                                                  \n\
    TMPDIR=\"$(mktemp -d)\"                                                 \n\
    pushd \"$TMPDIR\"                                                       \n\
        git clone https://github.com/ARMmbed/mbedtls.git                    \n\
        cd mbedtls                                                          \n\
        git checkout v3.1.0                                                 \n\
        scripts/config.py set MBEDTLS_USE_PSA_CRYPTO                        \n\
        cmake -DUSE_SHARED_MBEDTLS_LIBRARY=On -DCMAKE_INSTALL_PREFIX=/usr . \n\
        make                                                                \n\
        make install                                                        \n\
    popd                                                                    \n\
    rm -rf \"$TMPDIR\"                                                      \n\
                                                                            \n\
" | bash
