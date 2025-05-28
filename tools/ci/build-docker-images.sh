#!/usr/bin/env bash
# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

set -e

SCRIPT_DIR="$(dirname "$(readlink -f "$BASH_SOURCE")")"
ANJAY_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"

print_help() {
    cat <<EOF >&2
NAME
    $(basename "$0") - Builds internal Docker image for use in Gitlab CI.

SYNOPSIS
    $(basename "$0") [-h|--help]
    $(basename "$0") --version <version>

OPTIONS
    -v, --version <version>
            - image version.
    -h, --help
            - print help message and exit.

USAGE
    First step is to build docker image locally e.g.:
      $(basename "$0") --version 0.1

    Second step is to push built image to docker.io
      docker login docker.io
      docker push avsystemembedded/anjay-travis:<image_version>
      docker logout
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -v|--version)                IMAGE_VERSION="$2"; shift ;;
        -h|--help)
            print_help
            exit 0
            ;;
        *)
            echo "unrecognized option: $1; use -h or --help for help"
            die
            ;;
    esac

    shift
done

 [[ "$IMAGE_VERSION" ]] || (echo "ERROR: Missing image version" && exit 1)

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
