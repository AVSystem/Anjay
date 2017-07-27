#!/bin/bash
#
# Copyright 2017 AVSystem <avsystem@avsystem.com>
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

set -e
if [ -n "$DOCKER_IMAGE" ]; then
    replace "<CC_PKG>" "$ANJAY_CC" \
            "<CXX_PKG>" "$ANJAY_CXX" \
            "<DEVCONFIG_FLAGS>" "$DEVCONFIG_FLAGS" \
            "<COVERITY_SCAN_TOKEN>" "$COVERITY_SCAN_TOKEN" \
            "<COVERITY_EMAIL>" "$COVERITY_EMAIL" \
            -- "travis/$DOCKER_IMAGE"/Dockerfile
    docker build -t "$DOCKER_IMAGE" -f "travis/$DOCKER_IMAGE/Dockerfile" .
    docker run -e CC="$ANJAY_CC" \
               -e CXX="$ANJAY_CXX" \
               -e PYMBEDTLS_CC="$(if [ "$PYMBEDTLS_CC" ]; then echo "$PYMBEDTLS_CC"; else echo "$ANJAY_CC"; fi)" \
               "$DOCKER_IMAGE"
else
    ./devconfig $DEVCONFIG_FLAGS && make -j && make check
fi
