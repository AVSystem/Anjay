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

import os
import shutil
import sys
import time
import traceback
import unittest

from .test_suite import get_test_name, get_suite_name, LogType

COLOR_DEFAULT = '\033[0m'
COLOR_YELLOW = '\033[0;33m'
COLOR_GREEN = '\033[0;32m'
COLOR_RED = '\033[0;31m'
COLOR_BLUE = '\033[0;34m'


class ResultStream:
    def __init__(self, stream):
        width = shutil.get_terminal_size(fallback=(80, 24))[0]

        self.stream = stream
        self.colorize = os.isatty(stream.fileno())

        self.test_name_indent = 2
        self.test_result_width = len('OK (999.99s)') + self.test_name_indent
        self.test_name_limit = width - self.test_name_indent - len(' ') - self.test_result_width

        self.suite_result_width = self.test_result_width
        self.suite_name_limit = width - len(' ') - self.suite_result_width

    def _write_colored(self, color, text):
        if self.colorize:
            self.stream.write('%s%s%s' % (color, text, COLOR_DEFAULT))
        else:
            self.stream.write(text)

    @staticmethod
    def _pad_with_dots(text, desired_length):
        extra_space = desired_length - len(text) - 1
        return (text
                + (' ' if extra_space % 2 else '')
                + (' .' * (extra_space // 2)))

    @staticmethod
    def _limit_text_size(text, limit):
        if len(text) > limit:
            return text[:limit - 3] + '...'
        return text

    def write_suite_name(self, suite_name):
        suite_name = self._limit_text_size(suite_name, self.suite_name_limit)

        self._write_colored(COLOR_YELLOW, suite_name + '\n')

    def write_suite_result(self, suite_name, result):
        suite_name = self._limit_text_size(suite_name, self.suite_name_limit)
        suite_name = self._pad_with_dots(suite_name, self.suite_name_limit)

        color = COLOR_GREEN if result.testsFailed == 0 else COLOR_RED
        self._write_colored(COLOR_YELLOW, suite_name + ' ')
        self._write_colored(color, '%d/%d\n' % (result.testsPassed, result.testsRun))

    def write_test_name(self, test_name):
        test_name = self._limit_text_size(test_name, self.test_name_limit)
        test_name = self._pad_with_dots(test_name, self.test_name_limit)

        fmt = '%%%ds%%s ' % (self.test_name_indent)
        self._write_colored(COLOR_DEFAULT, fmt % ('', test_name))

    def write_test_success(self, seconds_elapsed):
        time_color = (COLOR_GREEN if seconds_elapsed < 5.0
                      else COLOR_YELLOW if seconds_elapsed < 30.0
        else COLOR_RED)

        self._write_colored(COLOR_GREEN, 'OK ')
        self._write_colored(time_color, '(%.2fs)\n' % seconds_elapsed)

    def write_test_failure(self, header, test, err):
        error_msg = 'Type: %s\nValue: %s\nStack trace:\n%s\n' % (
            err[0].__name__, err[1], ''.join(traceback.format_tb(err[2])))

        self._write_colored(COLOR_RED, header + '\nTest ')
        self._write_colored(COLOR_YELLOW, get_test_name(test))
        self._write_colored(COLOR_RED, ' failed!\n')
        self._write_colored(COLOR_DEFAULT, error_msg)

    def write_test_skip(self, reason):
        text = 'SKIP\n%s%s\n' % (' ' * (self.test_name_indent * 2), reason)
        self._write_colored(COLOR_BLUE, text)


class PrettyTestResult(unittest.TestResult):
    def __init__(self, suite, stream, logfile_stream, colorize=False):
        unittest.TestResult.__init__(self)
        self.suite = suite
        self.stream = stream
        self.logfile = logfile_stream
        self.times = {}
        self.successes = []

    def startTest(self, test):
        self.logfile.write_test_name(get_test_name(test))
        self.stream.write_test_name(get_test_name(test))

        self.testsRun += 1
        self.times[test] = time.time()

    def addSuccess(self, test):
        seconds_elapsed = time.time() - self.times[test]

        self.logfile.write_test_success(seconds_elapsed)
        self.stream.write_test_success(seconds_elapsed)
        self.successes.append(test)

    def _logError(self, header, test, err):
        self.logfile.write_test_failure(header, test, err)
        self.stream.write_test_failure(header, test, err)

    def addError(self, test, err):
        self._logError('ERROR', test, err)
        self.errors.append((test, err))

    def addFailure(self, test, err):
        self._logError('FAIL', test, err)
        self.failures.append((test, err))

    def addSkip(self, test, reason):
        self.logfile.write_test_skip(reason)
        self.stream.write_test_skip(reason)

    def errorSummary(self, log_root):
        return ('\n'.join('-----\n'
                          '%s:\n'
                          '%s\n'
                          'Demo log: %s\n'
                          'Valgrind: %s\n'
                          'PCAP:     %s\n'
                          '-----\n'
                          % (get_test_name(test),
                             ''.join(traceback.format_exception(*err)),
                             test.logs_path(LogType.Console, log_root),
                             test.logs_path(LogType.Valgrind, log_root),
                             test.logs_path(LogType.Pcap, log_root))
                          for test, err in self.errors + self.failures))

    @property
    def testsPassed(self):
        return self.testsRun - len(set(x[0] for x in self.errors + self.failures))

    @property
    def testsErrors(self):
        # it's possible to get multiple errors from a single test
        return len(set(x[0] for x in self.errors))

    @property
    def testsFailed(self):
        return len(set(x[0] for x in self.failures))


class PrettyTestRunner(unittest.TextTestRunner):
    def __init__(self, config, stream=sys.stderr):
        self.stream = ResultStream(stream)
        self.results = []
        self.config = config

    def run(self, suite, logfile):
        "Run given test suite."
        result = PrettyTestResult(suite, self.stream, ResultStream(logfile))

        suite_name = get_suite_name(suite)
        self.stream.write_suite_name(suite_name)

        suite(result)
        self.results.append(result)

        self.stream.write_suite_result(suite_name, result)
        return result
