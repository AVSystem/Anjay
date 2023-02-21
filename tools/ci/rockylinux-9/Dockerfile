# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

FROM rockylinux/rockylinux:9
RUN dnf update -y && \
    dnf install -y python3-pip git openssl-devel zlib-devel \
                   python3 python3-devel wget python3-cryptography openssl \
                   python3-requests python3-packaging valgrind curl cmake \
                   gcc gcc-c++ wireshark-cli which 'dnf-command(config-manager)'
RUN dnf config-manager --set-enabled crb && \
    dnf install -y epel-release && \
    dnf install -y mbedtls-devel
RUN pip3 install sphinx sphinx-rtd-theme cbor2 aiocoap wheel
# Solve issues with EPERM when running dumpcap
RUN setcap '' $(which dumpcap)
