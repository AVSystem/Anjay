#!/usr/bin/env python3
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.
import sys
import hashlib
import subprocess
from pathlib import Path


def _calculate_recursive_hash(directories):
    """Calculates a combined MD5 checksum for all files in provided directories."""
    md5 = hashlib.md5()

    for directory in directories:
        directory = Path(directory).resolve()
        if not directory.exists():
            continue

        all_files = sorted(p for p in directory.rglob('*') if p.is_file())

        for filepath in all_files:
            # Include filename in hash to detect renames
            md5.update(str(filepath.relative_to(directory)).encode('utf-8'))
            with open(filepath, 'rb') as f:
                md5.update(f.read())

    return md5.hexdigest()


def _main():
    if len(sys.argv) < 5:
        print(
            "Usage: python3 build_sphinx_docs.py <source_dir> <conf_dir> <output_dir> <cache_file> [extra_dependency_dirs...]")
        return 1

    source_dir = sys.argv[1]
    conf_dir = sys.argv[2]
    output_dir = sys.argv[3]
    cache_file = sys.argv[4]
    extra_deps = sys.argv[5:]

    scan_dirs = [source_dir] + extra_deps
    print(f"Scanning for changes in: {', '.join(scan_dirs)} ...")
    current_hash = _calculate_recursive_hash(scan_dirs)

    # Check stored hash
    input_hash_file = Path(cache_file).resolve()

    try:
        stored_hash = Path(input_hash_file).read_text(encoding='utf-8').strip()
    except OSError:
        stored_hash = ""
    if current_hash == stored_hash:
        print(f"--> No changes detected. Skipping Sphinx build.")
        return 0

    print(f"--> Changes detected. Running Sphinx...")

    # Construct Sphinx command
    # -j auto: Parallel build
    # -b html: HTML builder
    # -c conf_dir: Configuration directory
    # source_dir: Source files
    # output_dir: Output directory
    # We use "input_hash_file" for cache logic
    command = ["sphinx-build",
               "-j", "auto",
               "-b",
               "html",
               "-c", conf_dir,
               source_dir,
               output_dir]
    print(f"    Command: {' '.join(command)}")

    # Run Sphinx
    result = subprocess.run(command)

    if result.returncode != 0:
        print(f"Error: Sphinx failed with return code {result.returncode}")
        return result.returncode

    # Update cache
    input_hash_file.parent.mkdir(parents=True, exist_ok=True)
    try:
        with open(input_hash_file, 'w') as f:
            f.write(current_hash)
    except OSError as e:
        print(f"Warning: Could not save hash to {input_hash_file}: {e}")

    return 0


if __name__ == "__main__":
    sys.exit(_main())
