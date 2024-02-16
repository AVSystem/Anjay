# Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

FROM ubuntu:18.04
RUN apt-get update && \
    env DEBIAN_FRONTEND=noninteractive \
    apt-get install -y python3-pip git build-essential libmbedtls-dev \
        libssl-dev zlib1g-dev python3 libpython3-dev wget valgrind curl cmake \
        tshark
COPY requirements.txt requirements.txt
RUN pip3 install --upgrade pip
RUN env LANG=C.UTF-8 LC_ALL=C.UTF-8 pip3 install -r requirements.txt
# NOTE: The versions in requirements.txt install cleanly but don't work properly
# on Python 3.6
RUN pip3 install cryptography==2.1.4 cbor2==4.1.2 aiocoap==0.4b3
# Solve issues with EPERM when running dumpcap
RUN setcap '' $(which dumpcap)
