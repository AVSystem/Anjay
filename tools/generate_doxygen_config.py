#!/usr/bin/env python3
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.
#
# -----------------------------------------------------------------------------
# Anjay Doxygen Configuration Helper
#
# This script scans the example config files and extracts all defined macros
# (including those commented out with /* #undef ... */) to be used in the
# Doxygen PREDEFINED tag.
# -----------------------------------------------------------------------------
import re
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
EXAMPLE_CONFIGS_PATH = PROJECT_ROOT / "example_configs/linux_lwm2m11"

# configs paths relative to EXAMPLE_CONFIGS_PATH
CONFIGS = [
    "anjay/anjay_config.h",
    "avsystem/commons/avs_commons_config.h",
    "avsystem/coap/avs_coap_config.h"
]


def extract_macros_from_file(file_path):
    """
    Extracts macro names from a file to be used in PREDEFINED list.
    Handles both #define and /* #undef ... */
    """
    if not file_path.exists():
        print(f"[Error] File not found for macro extraction: {file_path}")
        sys.exit(1)

    with open(file_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    macros = []
    # Exclude these headers from macro extraction because if they are defined in
    # doxygen, it does not analyze the contents of config files
    EXCLUDED = {"ANJAY_CONFIG_H", "AVS_COMMONS_CONFIG_H", "AVS_COAP_CONFIG_H"}

    for line in lines:
        # Match #define MACRO
        match_def = re.match(r'^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)', line)
        if match_def:
            macro = match_def.group(1)
            if macro not in EXCLUDED:
                macros.append(macro)
                continue

        # Match /* #undef MACRO */
        match_undef = re.match(
            r'^\s*/\*\s*#undef\s+([A-Za-z_][A-Za-z0-9_]*)', line)
        if match_undef:
            macro = match_undef.group(1)
            if macro not in EXCLUDED:
                macros.append(macro)

    return macros


def run_generate_predefined(output_defines_file):
    """Executes the generate-predefined logic."""
    all_macros = []
    for config in CONFIGS:
        # Resolve path relative to script location -> root -> example configs
        src_file = EXAMPLE_CONFIGS_PATH / config
        all_macros.extend(extract_macros_from_file(src_file))

    # Determine absolute path for output
    output_defines_path = Path(output_defines_file).resolve()

    # Ensure directory exists
    output_defines_path.parent.mkdir(parents=True, exist_ok=True)

    # Write content
    new_content = ' '.join(all_macros)
    with open(output_defines_path, 'w', encoding='utf-8') as f:
        f.write(new_content)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python generate_doxygen_config.py <output_defines_file>")
        sys.exit(1)

    output_defines_file = sys.argv[1]
    run_generate_predefined(output_defines_file)
