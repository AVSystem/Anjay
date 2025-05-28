# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

# NOTE: After changing this file, please remember to update the Docker image on
# Docker Hub (for more information, check the help message of 
# tools/ci/build-docker-images.sh script).

FROM rockylinux/rockylinux:9
RUN dnf update -y && \
    dnf install -y --allowerasing python3-pip git openssl-devel zlib-devel \
                   python3 python3-devel wget openssl valgrind curl cmake \
                   gcc gcc-c++ wireshark-cli which 'dnf-command(config-manager)'
RUN dnf config-manager --set-enabled crb && \
    dnf install -y epel-release && \
    dnf install -y mbedtls-devel
COPY requirements.txt requirements.txt
RUN pip3 install -r requirements.txt
