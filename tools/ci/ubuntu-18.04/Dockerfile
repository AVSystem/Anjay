# Copyright 2017-2022 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

FROM ubuntu:18.04
RUN apt-get update && \
    env DEBIAN_FRONTEND=noninteractive \
    apt-get install -y python3-pip git build-essential libmbedtls-dev \
        libssl-dev zlib1g-dev python3 libpython3-dev wget python3-cryptography \
        python3-requests python3-packaging valgrind curl cmake
RUN pip3 install sphinx sphinx-rtd-theme
# NOTE: Newer versions don't install cleanly on Python 3.6
RUN pip3 install aiocoap==0.4b3 cbor2==4.1.2
