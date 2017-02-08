#!/usr/bin/env python3

import sys
assert sys.version_info >= (3,5), "Python < 3.5 is unsupported"

import unittest
import os
import re
import collections
import argparse
import string
import time
import tempfile
import shutil

from framework.pretty_test_runner import PrettyTestRunner, get_test_name, get_suite_name, get_full_test_name
from framework.pretty_test_runner import COLOR_DEFAULT, COLOR_YELLOW, COLOR_GREEN, COLOR_RED
from framework.test_suite import Lwm2mTest, ensure_dir

if sys.version_info[0] >= 3:
    sys.stderr = os.fdopen(2, 'w', 1) # force line buffering

ROOT_DIR = os.path.abspath(os.path.dirname(__file__))
DEFAULT_DEMO_PATH = os.path.abspath(ROOT_DIR + '/../../bin')
DEFAULT_DEMO_EXECUTABLE = 'demo'
UNITTEST_PATH = os.path.join(ROOT_DIR, 'suites')
DEFAULT_SUITE_REGEX = r'^default/'

def traverse(tree, cls=None):
    if cls is None or isinstance(tree, cls):
        yield tree

    if isinstance(tree, collections.Iterable):
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
        print('* %s: %s' % (test.suite_name(), test.test_name()))
    print('')


def run_tests(suites, config):
    has_error = False

    test_runner = PrettyTestRunner(config)

    start_time = time.time()
    for suite in suites:
        if suite.countTestCases() == 0:
            continue

        log_dir = os.path.join(config.logs_path, 'test')
        ensure_dir(log_dir)
        log_filename = os.path.join(log_dir, '%s.log' % (get_suite_name(suite),))

        with open(log_filename, 'w') as logfile:
            res = test_runner.run(suite, logfile)
            if not res.wasSuccessful():
                has_error = True

    seconds_elapsed = time.time() - start_time
    all_tests = sum(r.testsRun for r in test_runner.results)
    successes = sum(r.testsPassed for r in test_runner.results)
    errors = sum(r.testsErrors for r in test_runner.results)
    failures = sum(r.testsFailed for r in test_runner.results)

    print('\nFinished in %f s; %s%d/%d successes%s, %s%d/%d errors%s, %s%d/%d failures%s\n'
          % (seconds_elapsed,
             COLOR_GREEN if successes == all_tests else COLOR_YELLOW, successes, all_tests, COLOR_DEFAULT,
             COLOR_RED if errors else COLOR_GREEN, errors, all_tests, COLOR_DEFAULT,
             COLOR_RED if failures else COLOR_GREEN, failures, all_tests, COLOR_DEFAULT))

    if has_error:
        for r in test_runner.results:
            print(r.errorSummary(log_root=config.target_logs_path))

        raise SystemError("Some tests failed, inspect log for details")


def filter_tests(suite, query_regex):
    matching_tests = []

    for test in suite:
        if isinstance(test, unittest.TestCase):
            name = get_test_name(test)
            if (re.search(query_regex, get_test_name(test))
                    or re.search(query_regex, get_full_test_name(test))):
                matching_tests.append(test)
        elif isinstance(test, unittest.TestSuite):
            if test.countTestCases() == 0:
                continue

            if re.search(query_regex, get_suite_name(test)):
                matching_tests.append(test)
            else:
                matching_suite = filter_tests(test, query_regex)
                if matching_suite.countTestCases() > 0:
                    matching_tests.append(matching_suite)

    return unittest.TestSuite(matching_tests)

def merge_directory(src, dst):
    for item in os.listdir(src):
        src_item = os.path.join(src, item)
        dst_item = os.path.join(dst, item)

        if os.path.isdir(src_item):
            merge_directory(src_item, dst_item)
        else:
            ensure_dir(os.path.dirname(dst_item))
            shutil.copy2(src_item, dst_item)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--list', '-l',
                        action='store_true',
                        help='only list matching test cases, do not execute them')
    parser.add_argument('--client', '-c',
                        type=str, default=os.path.join(DEFAULT_DEMO_PATH, DEFAULT_DEMO_EXECUTABLE),
                        help='path to the demo application to use')
    parser.add_argument('query_regex',
                        type=str, default=DEFAULT_SUITE_REGEX, nargs='?',
                        help='regex used to filter test cases, run against suite name, test case name and "suite: test" string')

    cmdline_args = parser.parse_args(sys.argv[1:])

    with tempfile.TemporaryDirectory() as tmp_log_dir:
        class TestConfig:
            demo_cmd = os.path.basename(cmdline_args.client)
            demo_path = os.path.abspath(os.path.dirname(cmdline_args.client))
            logs_path = tmp_log_dir
            suite_root_path = os.path.abspath(UNITTEST_PATH)

            target_logs_path = os.path.abspath(os.path.join(demo_path, '../test/integration/log'))

        def config_to_string(cfg):
            config = sorted((k, v) for k, v in cfg.__dict__.items()
                            if not k.startswith('_')) # skip builtins

            max_key_len = max(len(k) for k, _ in config)

            return '\n  '.join(['Test config:'] + ['%%-%ds = %%s' % max_key_len % kv for kv in config])

        test_suites = discover_test_suites(TestConfig)
        header = '%d tests:' % test_suites.countTestCases()

        if cmdline_args.query_regex:
            test_suites = filter_tests(test_suites, cmdline_args.query_regex)
            header = '%d tests match pattern %s:' % (test_suites.countTestCases(), cmdline_args.query_regex)

        list_tests(test_suites, header=header)

        result = None
        if not cmdline_args.list:
            sys.stderr.write('%s\n\n' % config_to_string(TestConfig))

            try:
                run_tests(test_suites, TestConfig)
            finally:
                # calculate logs path based on executable path to prevent it
                # from creating files in source directory if building out of source
                ensure_dir(os.path.dirname(TestConfig.target_logs_path))
                merge_directory(TestConfig.logs_path, TestConfig.target_logs_path)
