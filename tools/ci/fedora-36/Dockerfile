# Copyright 2017-2022 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

FROM fedora:36
RUN dnf update -y && \
    dnf install -y python3-pip git mbedtls-devel openssl-devel zlib-devel \
                   python3 python3-devel wget python3-cryptography openssl \
                   python3-requests python3-packaging valgrind curl cmake \
                   gcc gcc-c++
RUN pip3 install sphinx sphinx-rtd-theme cbor2 aiocoap wheel
