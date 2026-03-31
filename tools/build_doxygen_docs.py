#!/usr/bin/env python3
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
EXAMPLE_CONFIGS_PATH = PROJECT_ROOT / "example_configs/linux_lwm2m11"
INCLUDE_PUBLIC_PATH = PROJECT_ROOT / "include_public"

# Configs to patch (relative to EXAMPLE_CONFIGS_PATH / INCLUDE_PUBLIC_PATH)
CONFIGS = [
    "anjay/anjay_config.h",
    "avsystem/commons/avs_commons_config.h",
    "avsystem/coap/avs_coap_config.h"
]


def patch_single_file(src_file, dest_file):
    """
    Reads a source file, replaces commented out undefs (/* #undef X */)
    with defines (#define X), and writes the result to the destination.
    """
    with open(src_file, 'r', encoding='utf-8') as f:
        content = f.read()

    # Enable disabled macros
    patched_content = re.sub(
        r'/\*\s*#undef\s+(\w+)\s*\*/', r'#define \1', content)

    # Ensure destination directory exists
    dest_file.parent.mkdir(parents=True, exist_ok=True)

    with open(dest_file, 'w', encoding='utf-8') as f:
        f.write(patched_content)


def stage_files(doxygen_input_dir):
    """
    Copies include_public headers and patches example configs into doxygen_input_dir/include_public.
    """
    dest_include_public = doxygen_input_dir / 'include_public'

    # 1. Clean destination if exists to ensure no stale files
    if dest_include_public.exists():
        shutil.rmtree(dest_include_public)

    # 2. Copy all include_public content
    if INCLUDE_PUBLIC_PATH.exists():
        shutil.copytree(INCLUDE_PUBLIC_PATH,
                        dest_include_public, dirs_exist_ok=True)

    # 3. Patch and copy configs over the copied headers
    for config in CONFIGS:
        src = EXAMPLE_CONFIGS_PATH / config
        dst = dest_include_public / config
        if src.exists():
            patch_single_file(src, dst)
        else:
            print(f"Warning: Source config not found: {src}")


def calculate_checksum(directory, doxyfile_path):
    """
    Calculates a combined MD5 checksum for:
    1. The Doxygen configuration file.
    2. All .h files found recursively in the provided directory.
    """
    md5 = hashlib.md5()

    # 1. Update hash with the content of the Doxyfile
    with open(doxyfile_path, 'rb') as f:
        md5.update(f.read())

    # 2. Walk through the directory and collect .h files
    all_header_files = sorted(p for p in Path(directory).rglob('*.h'))

    for filepath in all_header_files:
        # Include filename in hash to detect renames
        md5.update(str(filepath.relative_to(directory)).encode('utf-8'))
        with open(filepath, 'rb') as f:
            md5.update(f.read())

    return md5.hexdigest()


def _main():
    if len(sys.argv) < 4:
        print("Usage: python build_doxygen_docs.py <doxygen_input_dir> <doxyfile_path> <hash_cache_file>")
        return 1

    doxygen_input_dir = Path(sys.argv[1]).resolve()
    doxyfile_path = Path(sys.argv[2]).resolve()
    hash_cache_file = Path(sys.argv[3]).resolve()

    # Ensure we are running from a location where relative paths are valid
    if not INCLUDE_PUBLIC_PATH.exists():
        print(
            f"Error: Could not find {INCLUDE_PUBLIC_PATH}. Run from project root.")
        return 1

    print(f"Staging files to {doxygen_input_dir} ...")

    # 1. Stage files (copy & patch)
    stage_files(doxygen_input_dir)

    staging_include_public = doxygen_input_dir / 'include_public'

    # 2. Calculate checksum
    current_hash = calculate_checksum(staging_include_public, doxyfile_path)

    # 3. Check cache
    try:
        stored_hash = hash_cache_file.read_text(encoding='utf-8').strip()
    except OSError:
        stored_hash = ""

    if current_hash == stored_hash:
        print(
            "--> No changes detected in public headers or Doxyfile. Skipping Doxygen build.")
        return 0

    print("--> Changes detected! Running Doxygen...")

    # 4. Run Doxygen
    # Doxyfile is already configured (via CMake) to look at doxygen_input_dir
    result = subprocess.run(["doxygen", str(doxyfile_path)], check=False)

    if result.returncode != 0:
        print(f"Error: Doxygen failed with return code {result.returncode}")
        return result.returncode

    print("--> Doxygen build complete.")

    # 5. Update cache
    try:
        hash_cache_file.parent.mkdir(parents=True, exist_ok=True)
        with open(hash_cache_file, 'w') as f:
            f.write(current_hash)
        print("Checksum updated.")
    except OSError as e:
        print(f"Warning: Could not save checksum to {hash_cache_file}: {e}")

    return 0


if __name__ == "__main__":
    sys.exit(_main())
