# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


FROM ubuntu:20.04

WORKDIR /Anjay

RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -yq git build-essential cmake libmbedtls-dev zlib1g-dev

COPY . .

RUN cmake .
RUN make -j

ENV HOME /Anjay

