#!/usr/bin/env bash
#
# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

set -e

EXCEPTIONS=(
    "^\./\.git/"
    "^\./examples/"
    "^\./test_ghactions"
    "/doc/sphinx/html/"
)

cd "$(dirname "$(dirname "$0")")"

find . -type l | grep -v -f <(for ((I=0; I<"${#EXCEPTIONS[@]}"; ++I)); do
    echo "${EXCEPTIONS[I]}"
done) | {
    FOUND=0
    while read REPLY; do
        echo "$REPLY"
        FOUND=1
    done

    exit "$FOUND"
}
