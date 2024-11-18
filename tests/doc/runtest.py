#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import argparse
import datetime
import errno
import logging
import os
import re
import subprocess
import sys
import urllib
import urllib.parse
from collections import defaultdict

import requests

PROJECT_ROOT = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..'))
HTTP_STATUS_OK = 200
FILE_EXTENSION = '.rst'
REGEX = r'<(http.*?)>`_'
PUBLIC_REPO_BLOB_PREFIX = 'https://github.com/AVSystem/Anjay/blob/master/'
PUBLIC_REPO_TREE_PREFIX = 'https://github.com/AVSystem/Anjay/tree/master/'
DEFAULT_DOC_PATH = os.path.join(PROJECT_ROOT, 'doc/sphinx/source')
# User-Agent taken from Stack Overflow, some websites need it
USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"


def _get_ignored_patterns():
    whitelist = os.environ.get('ANJAY_DOC_CHECK_WHITELIST') or ''
    date, patterns = (whitelist.split('=', 1) + [''])[:2]
    patterns = patterns.split(',')
    if date == str(datetime.datetime.now().date()):
        return patterns
    else:
        return []


IGNORED_PATTERNS = _get_ignored_patterns()


def explore(path):
    for root, directories, file_names in os.walk(path):
        for file_name in file_names:
            file_path = os.path.join(root, file_name)
            if file_path.lower().endswith(FILE_EXTENSION):
                with open(file_path, encoding="utf-8") as f:
                    content = f.read()
                    yield (file_path, content)


def find_urls(rst_content):
    lines = enumerate(rst_content.splitlines(), 1)
    return ((line_number, found_url)
            for line_number, line_content in lines
            for found_url in re.findall(REGEX, line_content))


class UrlChecker:
    def __init__(self, max_attempts=5):
        super().__init__()
        self.max_attempts = max_attempts

    def is_url_valid(self, url, attempt=1):
        if url.startswith(PUBLIC_REPO_BLOB_PREFIX):
            return os.path.isfile(os.path.join(PROJECT_ROOT, url[len(PUBLIC_REPO_BLOB_PREFIX):]))
        elif url.startswith(PUBLIC_REPO_TREE_PREFIX):
            return os.path.isdir(os.path.join(PROJECT_ROOT, url[len(PUBLIC_REPO_TREE_PREFIX):]))
        if any(pattern in url for pattern in IGNORED_PATTERNS):
            logging.warning(
                'URL %s not checked due to ANJAY_DOC_CHECK_WHITELIST' % (url,))
            return True

        if attempt > self.max_attempts:
            logging.error(
                'URL %s could not be reached %d times. Giving up.' % (url, self.max_attempts))
            logging.error(('If you believe this is a problem on the remote site, you can set the '
                           + 'ANJAY_DOC_CHECK_WHITELIST=%s=%s environment variable to ignore this '
                           + 'error for today.')
                          % (datetime.datetime.now().date(),
                             ','.join(IGNORED_PATTERNS + [urllib.parse.urlparse(url).hostname])))
            return False

        try:
            self.perform_request(url)
        except:
            logging.warning('URL %s could not be reached (%d/%d): %s'
                            % (url, attempt, self.max_attempts, sys.exc_info()[0]))
            return self.is_url_valid(url, attempt + 1)

        return True


class RequestsUrlChecker(UrlChecker):
    def perform_request(self, url):
        status = requests.head(url, allow_redirects=True, timeout=10,
                               headers={"User-Agent": USER_AGENT})
        if not status:
            raise IOError(errno.EIO, 'Invalid HTTP status')
        if status.status_code != HTTP_STATUS_OK:
            raise IOError(errno.EIO, f'HTTP status: {status.status_code}')


class Wget2UrlChecker(UrlChecker):
    def __init__(self, *args, **kwargs):
        subprocess.run(['wget2', '--version'], stdout=subprocess.DEVNULL, check=True)
        super().__init__(*args, **kwargs)

    def perform_request(self, url):
        # Despite the wget2 can handle HTTP/2, there are few servers that use specific implementation
        # of HTTP/2, which is not handled properly by this tool. That's why the --no-http2 option is used.
        result = subprocess.run(['wget2', '--no-http2', '--wait=1', '--random-wait', '-q', '-t', '1', '-U',
                                 USER_AGENT,'-T', '10', '--prefer-family=IPv4', '-O', '/dev/null',
                                 '--stats-site=csv:-', '--', url],
                                stdout=subprocess.PIPE, check=True)
        csv_lines = result.stdout.strip().split(b'\n')
        header_line = csv_lines[0].split(b',')
        last_line = csv_lines[-1].split(b',')
        # We use negative index because the URL column may contain commas
        # and wget2 does not escape properly
        status_index = header_line.index(b'Status') - len(header_line)
        http_status = last_line[status_index]
        if http_status != str(HTTP_STATUS_OK).encode():
            raise IOError(errno.EIO, f'HTTP status: {http_status.decode()}')


def find_invalid_urls(checker, urls):
    from multiprocessing import pool

    with pool.Pool(2 * os.cpu_count()) as p:
        responses = p.map(checker.is_url_valid, urls.keys())

    invalid_urls = defaultdict(list)
    for url, url_valid in zip(urls, responses):
        if not url_valid:
            invalid_urls[url] = urls[url]

    return invalid_urls


def report(path):
    try:
        checker = Wget2UrlChecker()
    except:
        checker = RequestsUrlChecker()

    urls = defaultdict(list)
    for file_path, content in explore(path):
        for line_number, found_url in find_urls(content):
            urls[found_url].append((file_path, line_number))

    invalid_urls = find_invalid_urls(checker, urls)
    if invalid_urls:
        logging.warning('There are invalid urls.')
        for (url, details) in invalid_urls.items():
            print('URL: %s in:' % url)
            for item in details:
                print('\t%s:%s' % item)
        if not isinstance(checker, Wget2UrlChecker):
            logging.warning("You may try installing 'wget2' to enable alternate URL checking logic.")
        sys.exit(-1)
    else:
        logging.info('All urls are valid.')


if __name__ == "__main__":
    logging.getLogger().setLevel(logging.INFO)
    parser = argparse.ArgumentParser(
        description='Explores RST files in specified or default path and validates URLs included in them.')
    parser.add_argument('doc_path',
                        type=str, default=DEFAULT_DOC_PATH, nargs='?',
                        help='path to the doc folder')
    cmdline_args = parser.parse_args()
    report(cmdline_args.doc_path)
