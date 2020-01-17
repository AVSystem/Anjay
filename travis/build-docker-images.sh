#!/usr/bin/env bash
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

set -e

SCRIPT_DIR="$(dirname "$(readlink -f "$BASH_SOURCE")")"

if [[ "$#" -gt 0 ]]; then
    cat <<EOF >&2
Builds Docker images for use in Travis CI.

Intended usage:

    # build docker images locally
    $0
    # push built images to docker.io
    docker login docker.io
    docker push avsystemembedded/anjay-travis:IMAGE_TO_PUSH
    docker logout

EOF
    exit 1
fi

build-docker-image() {
    local NAME="$1"
    local DOCKERFILE="$2"
    docker build --no-cache -t "$NAME" -f "$DOCKERFILE" "$(dirname "$DOCKERFILE")"
}

for IMAGE_DIR in "$SCRIPT_DIR/base-images/"*; do
    IMAGE_NAME="avsystemembedded/anjay-travis:$(basename "$IMAGE_DIR")"
    build-docker-image "$IMAGE_NAME" "$IMAGE_DIR/Dockerfile"
done
