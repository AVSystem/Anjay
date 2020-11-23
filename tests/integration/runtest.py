#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import sys

assert sys.version_info >= (3, 5), "Python < 3.5 is unsupported"

import unittest
import os
import collections.abc
import argparse
import time
import tempfile
import textwrap
import shutil
import logging

from framework.pretty_test_runner import PrettyTestRunner
from framework.pretty_test_runner import COLOR_DEFAULT, COLOR_YELLOW, COLOR_GREEN, COLOR_RED
from framework.test_suite import Lwm2mTest, ensure_dir, get_full_test_name, get_suite_name, \
    test_or_suite_matches_query_regex, LogType

if sys.version_info[0] >= 3:
    sys.stderr = os.fdopen(2, 'w', 1)  # force line buffering

ROOT_DIR = os.path.abspath(os.path.dirname(__file__))
UNITTEST_PATH = os.path.join(ROOT_DIR, 'suites')
DEFAULT_SUITE_REGEX = r'^default\.'


def traverse(tree, cls=None):
    if cls is None or isinstance(tree, cls):
        yield tree

    if isinstance(tree, collections.abc.Iterable):
        for elem in tree:
            for sub_elem in traverse(elem, cls):
                yield sub_elem


def discover_test_suites(test_config):
    loader = unittest.TestLoader()
    loader.testMethodPrefix = 'runTest'
    suite = loader.discover(UNITTEST_PATH, pattern='*.py', top_level_dir=UNITTEST_PATH)

    for error in loader.errors:
        print(error)
    if len(loader.errors):
        sys.exit(-1)

    for test in traverse(suite, cls=Lwm2mTest):
        test.set_config(test_config)
    return suite


def list_tests(suite, header='Available tests:'):
    print(header)
    for test in traverse(suite, cls=Lwm2mTest):
        print('* %s' % get_full_test_name(test))
    print('')


def run_tests(suites, config):
    test_runner = PrettyTestRunner(config)

    start_time = time.time()
    for suite in suites:
        if suite.countTestCases() == 0:
            continue

        log_dir = os.path.join(config.logs_path, 'test')
        ensure_dir(log_dir)
        log_filename = os.path.join(log_dir, '%s.log' % (get_suite_name(suite),))

        with open(log_filename, 'w') as logfile:
            test_runner.run(suite, logfile)

    seconds_elapsed = time.time() - start_time
    all_tests = sum(r.testsRun for r in test_runner.results)
    successes = sum(r.testsPassed for r in test_runner.results)
    errors = sum(r.testsErrors for r in test_runner.results)
    failures = sum(r.testsFailed for r in test_runner.results)

    print('\nFinished in %f s; %s%d/%d successes%s, %s%d/%d errors%s, %s%d/%d failures%s\n'
          % (seconds_elapsed,
             COLOR_GREEN if successes == all_tests else COLOR_YELLOW, successes, all_tests,
             COLOR_DEFAULT,
             COLOR_RED if errors else COLOR_GREEN, errors, all_tests, COLOR_DEFAULT,
             COLOR_RED if failures else COLOR_GREEN, failures, all_tests, COLOR_DEFAULT))

    return test_runner.results


def filter_tests(suite, query_regex):
    matching_tests = []

    for test in suite:
        if isinstance(test, unittest.TestCase):
            if test_or_suite_matches_query_regex(test, query_regex):
                matching_tests.append(test)
        elif isinstance(test, unittest.TestSuite):
            if test.countTestCases() == 0:
                continue

            if test_or_suite_matches_query_regex(test, query_regex):
                matching_tests.append(test)
            else:
                matching_suite = filter_tests(test, query_regex)
                if matching_suite.countTestCases() > 0:
                    matching_tests.append(matching_suite)

    return unittest.TestSuite(matching_tests)


def merge_directory(src, dst):
    """
    Move all contents of SRC into DST, preserving directory structure.
    """
    for item in os.listdir(src):
        src_item = os.path.join(src, item)
        dst_item = os.path.join(dst, item)

        if os.path.isdir(src_item):
            merge_directory(src_item, dst_item)
        else:
            ensure_dir(os.path.dirname(dst_item))
            shutil.move(src_item, dst_item)


def remove_tests_logs(tests):
    for test in tests:
        for log_type in LogType:
            try:
                os.remove(test.logs_path(log_type))
            except FileNotFoundError:
                pass


if __name__ == "__main__":
    LOG_LEVEL = os.getenv('LOGLEVEL', 'info').upper()
    try:
        import coloredlogs

        coloredlogs.install(level=LOG_LEVEL)
    except ImportError:
        logging.basicConfig(level=LOG_LEVEL)

    parser = argparse.ArgumentParser(description=textwrap.dedent('''
        Runs Anjay demo client against Python integration tests.

        Following environment variables are recognized:

          VALGRIND - can be set to Valgrind executable path + optional arguments. If set,
                     demo client execution command will be prefixed with the value of this
                     variable. Note that some tests ignore this command.

          NO_DUMPCAP - if set and not empty, PCAP traffic recordings between demo client
                       and mock server are not recorded.

          RR - if set and not empty, demo client execution command is prefixed
               with `rr record` to allow post-mortem debugging with `rr replay`.
               Takes precedence over RRR.

          RRR - if set and not empty, its value is is used for regex-matching applicable
                tests or test suites. See REGEX MATCH RULES below. For matching
                tests/suites, demo client execution command is prefixed with `rr record`
                to allow post-mortem debugging with `rr replay`.

        REGEX MATCH RULES
        =================
        {regex_match_rules_help}
    '''.format(regex_match_rules_help=textwrap.indent(
        textwrap.dedent(
            test_or_suite_matches_query_regex.__doc__),
        prefix=' ' * 8))),
        formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument('--list', '-l',
                        action='store_true',
                        help='only list matching test cases, do not execute them')
    parser.add_argument('--client', '-c',
                        type=str, required=True,
                        help='path to the demo application to use')
    parser.add_argument('--keep-success-logs',
                        action='store_true',
                        help='keep logs from all tests, including ones that passed')
    parser.add_argument('--target-logs-path', type=str,
                        help='path where to leave the logs stored')
    parser.add_argument('query_regex',
                        type=str, default=DEFAULT_SUITE_REGEX, nargs='?',
                        help='regex used to filter test cases. See REGEX MATCH RULES for details.')

    cmdline_args = parser.parse_args(sys.argv[1:])

    with tempfile.TemporaryDirectory() as tmp_log_dir:
        class TestConfig:
            demo_cmd = os.path.basename(cmdline_args.client)
            demo_path = os.path.abspath(os.path.dirname(cmdline_args.client))
            logs_path = tmp_log_dir
            suite_root_path = os.path.abspath(UNITTEST_PATH)

            target_logs_path = os.path.abspath(
                cmdline_args.target_logs_path or os.path.join(demo_path, '../test/integration/log'))


        def config_to_string(cfg):
            config = sorted((k, v) for k, v in cfg.__dict__.items()
                            if not k.startswith('_'))  # skip builtins

            max_key_len = max(len(k) for k, _ in config)

            return '\n  '.join(
                ['Test config:'] + ['%%-%ds = %%s' % max_key_len % kv for kv in config])


        test_suites = discover_test_suites(TestConfig)
        header = '%d tests:' % test_suites.countTestCases()

        if cmdline_args.query_regex:
            test_suites = filter_tests(test_suites, cmdline_args.query_regex)
            header = '%d tests match pattern %s:' % (
                test_suites.countTestCases(), cmdline_args.query_regex)

        list_tests(test_suites, header=header)

        result = None
        if not cmdline_args.list:
            sys.stderr.write('%s\n\n' % config_to_string(TestConfig))

            try:
                results = run_tests(test_suites, TestConfig)
                for r in results:
                    if r.errors or r.failures:
                        print(r.errorSummary(log_root=TestConfig.target_logs_path))
                    if not cmdline_args.keep_success_logs:
                        remove_tests_logs(r.successes)

                if any(r.errors or r.failures for r in results):
                    raise SystemError("Some tests failed, inspect log for details")
            finally:
                # calculate logs path based on executable path to prevent it
                # from creating files in source directory if building out of source
                ensure_dir(os.path.dirname(TestConfig.target_logs_path))
                merge_directory(TestConfig.logs_path, TestConfig.target_logs_path)
