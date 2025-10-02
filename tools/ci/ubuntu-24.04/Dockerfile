# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

# NOTE: After changing this file, please remember to update the Docker image on
# Docker Hub (for more information, check the help message of 
# tools/ci/build-docker-images.sh script).

FROM ubuntu:24.04
RUN apt-get update && \
    env DEBIAN_FRONTEND=noninteractive \
    apt-get install -y python3-pip git libmbedtls-dev libssl-dev zlib1g-dev \
        python3 libpython3-dev wget valgrind curl cmake build-essential tshark \
        file python3-venv

# set up Python3 venv
COPY requirements.txt requirements.txt
RUN python3 -m venv /venv
RUN . /venv/bin/activate && \
    pip install --upgrade pip && \
    pip install -r requirements.txt
ENTRYPOINT ["/bin/bash", "-c", ". /venv/bin/activate && exec \"$@\"", "--"]

# Solve issues with EPERM when running dumpcap
RUN setcap '' $(which dumpcap)
