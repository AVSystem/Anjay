# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.


FROM ubuntu:24.04

WORKDIR /Anjay

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -yq git build-essential cmake libmbedtls-dev zlib1g-dev

COPY . .

RUN cmake .
RUN make -j

ENV HOME /Anjay

