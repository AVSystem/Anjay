#!/usr/bin/env bash
# Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

set -e

SCRIPT_DIR="$(dirname "$(readlink -f "$BASH_SOURCE")")"
ANJAY_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"

if [[ $# -ne 2 || $1 != "--version" ]]; then
    cat <<EOF >&2
Builds Docker images for use in CI.

Intended usage:

    # build docker images locally
    $0 --version <image_version>
    # push built images to docker.io
    docker login docker.io
    docker push avsystemembedded/anjay-travis:IMAGE_TO_PUSH
    docker logout

EOF
    exit 1
fi

IMAGE_VERSION=$2

build-docker-image() {
    local NAME="$1"
    local DOCKERFILE="$2"
    pushd "$ANJAY_ROOT"
        docker build --no-cache -t "$NAME" -f "$DOCKERFILE" .
    popd
}

for IMAGE_DOCKERFILE in "$SCRIPT_DIR/"*/Dockerfile; do
    IMAGE_NAME="avsystemembedded/anjay-travis:$(basename "$(dirname "$IMAGE_DOCKERFILE")")-"$IMAGE_VERSION""
    build-docker-image "$IMAGE_NAME" "$IMAGE_DOCKERFILE"
done
