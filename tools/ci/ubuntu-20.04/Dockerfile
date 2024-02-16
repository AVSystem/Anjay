# Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

FROM ubuntu:20.04
RUN apt-get update && \
    env DEBIAN_FRONTEND=noninteractive \
    apt-get install -y python3-pip git libmbedtls-dev libssl-dev zlib1g-dev \
        python3 libpython3-dev wget valgrind curl cmake build-essential tshark
COPY requirements.txt requirements.txt
RUN pip3 install -r requirements.txt
# Solve issues with EPERM when running dumpcap
RUN setcap '' $(which dumpcap)
