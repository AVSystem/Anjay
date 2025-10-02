#!/usr/bin/env python3
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

# -----------------------------------------------------------------------------
# Anjay Doxygen Configuration Helper
#
# This script prepares source headers for Doxygen documentation generation.
# It supports three modes:
#
#   1. patch-config:
#      - Takes a sample anjay_config.h with disabled options (/* #undef ... */)
#      - Replaces them with #define macros so Doxygen can see all features
#      - Copies include_public/ and the patched config into an output directory
#
#   2. generate-predefined:
#      - Extracts all #define MACROs from a given anjay_config.h
#      - Outputs a space-separated list of macros for use as PREDEFINED in Doxygen
#
#   3. all:
#      - Runs patch-config and then generate-predefined in one pass
#
# Example usage from CMake:
#
#   bash tools/generate_doxygen_config.sh all \
#       path/to/example_configs/linux_lwm2m11/anjay/anjay_config.h \
#       path/to/include_public \
#       path/to/output/dir \
#       path/to/output/defines.txt
#
# This script is used during CMake configuration to generate Doxyfile input.
# -----------------------------------------------------------------------------
import shutil
import re
import sys
from pathlib import Path


def patch_config(example_config, include_public_src, output_root):
    example_config = Path(example_config).resolve()
    include_public_src = Path(include_public_src).resolve()
    output_root = Path(output_root).resolve()
    output_include = output_root / 'include_public'
    output_config = output_include / 'anjay' / 'anjay_config.h'

    shutil.copytree(include_public_src, output_include, dirs_exist_ok=True)
    output_config.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(example_config, output_config)

    with open(output_config, 'r', encoding='utf-8') as f:
        content = f.read()
    patched_content = re.sub(r'/\*\s*#undef\s+(\w+)\s*\*/', r'#define \1', content)
    with open(output_config, 'w', encoding='utf-8') as f:
        f.write(patched_content)

    print(f"[generate_doxygen_config.py] Patched config written to: {output_config}")


def generate_predefined(example_config, output_file):
    example_config = Path(example_config).resolve()
    output_file = Path(output_file).resolve()
    output_file.parent.mkdir(parents=True, exist_ok=True)

    with open(example_config, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    EXCLUDED_MACROS = {"ANJAY_CONFIG_H"}

    macros = []
    for line in lines:
        match = re.match(r'^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)', line)
        if match:
            macro = match.group(1)
            if macro not in EXCLUDED_MACROS:
                macros.append(macro)

        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(' '.join(macros))


def run_all(example_config, include_public_src, output_root, output_defines):
    patch_config(example_config, include_public_src, output_root)
    patched_config = Path(output_root) / 'include_public' / 'anjay' / 'anjay_config.h'
    generate_predefined(patched_config, output_defines)


if __name__ == '__main__':
    if len(sys.argv) < 5:
        print("Usage: python generate_doxygen_config.py <mode> <example_config> <include_public_src> <output_root> [output_defines]")
        sys.exit(1)

    mode = sys.argv[1]
    example_config = sys.argv[2]
    include_public_src = sys.argv[3]
    output_root = sys.argv[4]

    if mode == 'patch-config':
        patch_config(example_config, include_public_src, output_root)
    elif mode == 'generate-predefined':
        if len(sys.argv) < 6:
            print("Missing output_defines path for generate-predefined mode")
            sys.exit(1)
        generate_predefined(example_config, sys.argv[5])
    elif mode == 'all':
        if len(sys.argv) < 6:
            print("Missing output_defines path for all mode")
            sys.exit(1)
        run_all(example_config, include_public_src, output_root, sys.argv[5])
    else:
        print(f"Unknown mode: {mode}")
        sys.exit(1)
