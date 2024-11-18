# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.


FROM ubuntu:24.04
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive \
    apt-get install -yq git build-essential cmake zlib1g-dev doxygen python3 \
        libpython3-dev libssl-dev python3-pip clang-tools valgrind opensc \
        libengine-pkcs11-openssl docker.io nodejs curl jq automake python3-venv
COPY requirements.txt .
RUN python3 -m venv /venv
RUN . /venv/bin/activate && \
    pip install --upgrade pip && \
    pip install -r requirements.txt
ENTRYPOINT ["/bin/bash", "-c", ". /venv/bin/activate && exec \"$@\"", "--"]

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
