#!/usr/bin/env bash
#
# Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
# Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

set -e

PLATFORMIO_COMMAND="platformio"
REPO_ROOT=$(git rev-parse --show-toplevel)

if ! command -v $PLATFORMIO_COMMAND &> /dev/null; then
    echo -e "\033[31m\"$PLATFORMIO_COMMAND\" comand could not be found\033[0m"
    echo -e "Please install $PLATFORMIO_COMMAND locally (pip3 install $PLATFORMIO_COMMAND)"
    echo -e "OR, if installed using IDE, add $PLATFORMIO_COMMAND to PATH"
    exit 1
fi

build_examples() {
    # this configuration requires that:
    #   - platformio.ini exists inside the example directory
    #   - the example directory contains a src/ directory with both include and
    #     source files
    pushd $REPO_ROOT
        for EXAMPLE_DIR in platformio/examples/*/; do
            $PLATFORMIO_COMMAND ci --lib='.' -c $EXAMPLE_DIR/platformio.ini $EXAMPLE_DIR/src/
        done
    popd
}

build_examples
