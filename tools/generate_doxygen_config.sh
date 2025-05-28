#!/bin/bash
#
# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.
set -euo pipefail

EXAMPLE_CONFIG="$1"         # e.g. example_configs/linux_lwm2m11/anjay/anjay_config.h
INCLUDE_PUBLIC_SRC="$2"     # e.g. include_public/
OUTPUT_ROOT="$3"            # e.g. output/doc/doxygen/config/

# Absolute paths
EXAMPLE_CONFIG="$(realpath "$EXAMPLE_CONFIG")"
INCLUDE_PUBLIC_SRC="$(realpath "$INCLUDE_PUBLIC_SRC")"
mkdir -p "${OUTPUT_ROOT}"
OUTPUT_ROOT="$(realpath "${OUTPUT_ROOT}")"

# Destination paths
OUTPUT_INCLUDE="${OUTPUT_ROOT}/include_public"
OUTPUT_CONFIG="${OUTPUT_INCLUDE}/anjay/anjay_config.h"

# Copy full include_public tree
cp -r "${INCLUDE_PUBLIC_SRC}" "${OUTPUT_ROOT}"

# Copy example config into place
mkdir -p "$(dirname "${OUTPUT_CONFIG}")"
cp "${EXAMPLE_CONFIG}" "${OUTPUT_CONFIG}"

# Patch: replace /* #undef X */ → #define X
sed -i -E 's@/\* *#undef +([^ ]+) *\*/@#define \1@g' "${OUTPUT_CONFIG}"

echo "[generate_doxygen_config.sh] Patched config written to: ${OUTPUT_CONFIG}"
