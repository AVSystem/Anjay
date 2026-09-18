#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

"""Run the CodeChecker workflows configured for this repository."""

import argparse
import os
import shutil
import subprocess
import sys


PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
ANALYZERS = ('standard', 'cppcheck')
CI_CODE_QUALITY_REPORT = os.path.join(PROJECT_ROOT,
                                      'gl-code-quality-report.json')


def analysis_path(analyzer, *path):
    return os.path.join(PROJECT_ROOT, 'tools', 'codechecker', analyzer + '_analysis',
                        *path)


def run_codechecker(arguments):
    environment = os.environ.copy()
    if 'CC_ANALYZER_BIN' not in environment:
        clang = shutil.which('clang-20')
        clang_tidy = shutil.which('clang-tidy-20')
        if not clang or not clang_tidy:
            print('CC_ANALYZER_BIN is not set and clang-20 or clang-tidy-20 '
                  'could not be found. LLVM 20 is required for consistent '
                  'analysis results.', file=sys.stderr)
            return None
        # Use a fixed LLVM version so reports stay consistent across runs.
        environment['CC_ANALYZER_BIN'] = (
            'clangsa:{};clang-tidy:{}'.format(clang, clang_tidy))
    try:
        return subprocess.run(['CodeChecker'] + arguments, cwd=PROJECT_ROOT,
                              env=environment)
    except FileNotFoundError:
        print('CodeChecker was not found in PATH.', file=sys.stderr)
        return None


def analyze(args):
    reports_path = analysis_path(args.analyzer, 'reports')
    os.makedirs(reports_path, exist_ok=True)
    result = run_codechecker([
        'analyze',
        '--config', analysis_path(args.analyzer, 'config.json'),
        os.path.join(PROJECT_ROOT, 'compile_commands.json'),
        '--output', reports_path,
        '--clean',
    ])
    return 1 if result is None else result.returncode


def analyze_file(args):
    reports_path = analysis_path(args.analyzer, 'reports')
    os.makedirs(reports_path, exist_ok=True)
    result = run_codechecker([
        'analyze',
        '--config', analysis_path(args.analyzer, 'config.json'),
        args.file,
        '--output', reports_path,
    ])
    return 1 if result is None else result.returncode


def generate_diff(args):
    result = run_codechecker([
        'cmd', 'diff',
        '--basename', analysis_path(args.analyzer, 'current.baseline'),
        '--newname', analysis_path(args.analyzer, 'reports'),
        '--new',
    ])
    return 1 if result is None else result.returncode


def ci_generate_diff(args):
    # ``cmd diff`` uses a non-zero status for a non-empty diff
    generate_diff(args)
    output_directory = analysis_path(args.analyzer, 'diff_codeclimate')
    os.makedirs(output_directory, exist_ok=True)
    result = run_codechecker([
        'cmd', 'diff',
        '--basename', analysis_path(args.analyzer, 'current.baseline'),
        '--newname', analysis_path(args.analyzer, 'reports'),
        '--new',
        '--output', 'codeclimate',
        '--export-dir', output_directory,
        '--clean',
    ])
    if result is None:
        return 1

    codeclimate_output = os.path.join(output_directory, 'codeclimate_issues.json')
    if not os.path.isfile(codeclimate_output):
        print('{} does not exist.'.format(codeclimate_output), file=sys.stderr)
        return 1

    # copy the result so Gitlab can understand it
    shutil.copyfile(codeclimate_output, CI_CODE_QUALITY_REPORT)
    if result.returncode:
        print('To ignore these issues, update the baseline with:\n'
              '  python3 tools/codechecker/static_analyze.py update_baseline '
              '--analyzer {}'.format(args.analyzer), file=sys.stderr)
    return result.returncode


def update_baseline(args):
    baseline_path = analysis_path(args.analyzer, 'current.baseline')
    if os.path.exists(baseline_path):
        os.remove(baseline_path)
    result = run_codechecker([
        'parse',
        analysis_path(args.analyzer, 'reports'),
        '--export', 'baseline',
        '--output', baseline_path,
    ])
    return 1 if result is None else result.returncode


def versions(_args):
    print('CodeChecker version:', flush=True)
    codechecker_result = run_codechecker(['version'])

    print('\nAnalyzer versions:', flush=True)
    analyzers_result = run_codechecker(['analyzers', '--output', 'table'])

    if codechecker_result is None or analyzers_result is None:
        return 1
    return codechecker_result.returncode or analyzers_result.returncode


def add_analyzer_argument(parser):
    parser.add_argument('-a', '--analyzer', choices=ANALYZERS,
                        default='standard',
                        help='analyzer configuration to use (default: standard)')


def main():
    parser = argparse.ArgumentParser(
        description='Run configured CodeChecker static-analysis workflows.')
    subparsers = parser.add_subparsers(dest='command', required=True)

    analyze_parser = subparsers.add_parser('analyze',
                                           help='analyze compile_commands.json')
    add_analyzer_argument(analyze_parser)
    analyze_parser.set_defaults(handler=analyze)

    file_parser = subparsers.add_parser('analyze_file',
                                        help='analyze one source file')
    add_analyzer_argument(file_parser)
    file_parser.add_argument('file', help='source file to analyze')
    file_parser.set_defaults(handler=analyze_file)

    diff_parser = subparsers.add_parser('generate_diff',
                                         help='generate a diff against the baseline')
    add_analyzer_argument(diff_parser)
    diff_parser.set_defaults(handler=generate_diff)

    ci_diff_parser = subparsers.add_parser(
        'ci_generate_diff',
        help='generate and validate a Code Climate diff for CI')
    add_analyzer_argument(ci_diff_parser)
    ci_diff_parser.set_defaults(handler=ci_generate_diff)

    baseline_parser = subparsers.add_parser('update_baseline',
                                             help='replace the current baseline')
    add_analyzer_argument(baseline_parser)
    baseline_parser.set_defaults(handler=update_baseline)

    versions_parser = subparsers.add_parser(
        'versions',
        help='show CodeChecker and analyzer versions')
    versions_parser.set_defaults(handler=versions)

    args = parser.parse_args()
    return args.handler(args)


if __name__ == '__main__':
    sys.exit(main())
