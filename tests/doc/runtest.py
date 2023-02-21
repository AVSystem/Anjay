#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

import argparse
import datetime
import logging
import os
import re
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


def is_url_valid(url, attempt=1, max_attempts=5):
    if url.startswith(PUBLIC_REPO_BLOB_PREFIX):
        return os.path.isfile(os.path.join(PROJECT_ROOT, url[len(PUBLIC_REPO_BLOB_PREFIX):]))
    elif url.startswith(PUBLIC_REPO_TREE_PREFIX):
        return os.path.isdir(os.path.join(PROJECT_ROOT, url[len(PUBLIC_REPO_TREE_PREFIX):]))
    if any(pattern in url for pattern in IGNORED_PATTERNS):
        logging.warning(
            'URL %s not checked due to ANJAY_DOC_CHECK_WHITELIST' % (url,))
        return True

    if attempt > max_attempts:
        logging.error(
            'URL %s could not be reached %d times. Giving up.' % (url, max_attempts))
        logging.error(('If you believe this is a problem on the remote site, you can set the '
                       + 'ANJAY_DOC_CHECK_WHITELIST=%s=%s environment variable to ignore this '
                       + 'error for today.')
                      % (datetime.datetime.now().date(),
                         ','.join(IGNORED_PATTERNS + [urllib.parse.urlparse(url).hostname])))
        return False

    status = None
    exception = None
    try:
        # The browser is just copied from Stack, some websites needs it
        status = requests.head(url, allow_redirects=True, timeout=10, headers={
                               "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"})
    except:
        exception = sys.exc_info()[0]

    if not status or status.status_code != HTTP_STATUS_OK:
        logging.warning('URL %s could not be reached (%d/%d): %s'
                        % (url,
                           attempt,
                           max_attempts,
                           exception if exception is not None else status.status_code))
        return is_url_valid(url, attempt + 1)

    return True


def find_invalid_urls(urls):
    from multiprocessing import pool

    with pool.Pool(2 * os.cpu_count()) as p:
        responses = p.map(is_url_valid, urls.keys())

    invalid_urls = defaultdict(list)
    for url, url_valid in zip(urls, responses):
        if not url_valid:
            invalid_urls[url] = urls[url]

    return invalid_urls


def report(path):
    urls = defaultdict(list)
    for file_path, content in explore(path):
        for line_number, found_url in find_urls(content):
            urls[found_url].append((file_path, line_number))

    invalid_urls = find_invalid_urls(urls)
    if invalid_urls:
        logging.warning('There are invalid urls.')
        for (url, details) in invalid_urls.items():
            print('URL: %s in:' % url)
            for item in details:
                print('\t%s:%s' % item)
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
