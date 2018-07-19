#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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

import argparse
import logging
import os
import re
import requests
import sys
from functools import lru_cache

ROOT_DIR = os.path.abspath(os.path.dirname(__file__))
HTTP_STATUS_OK = 200
FILE_EXTENSION = '.rst'
REGEX = r'<(http.*?)>`_'
DEFAULT_DOC_PATH = os.path.normpath(os.path.join(ROOT_DIR, '../../doc/sphinx/source'))

@lru_cache()
def is_valid_url(url):
    logging.info('Processing %s', url)
    try:
        return requests.head(url, allow_redirects=True).status_code == HTTP_STATUS_OK
    except requests.ConnectionError:
        return False

def explore(path):
    for root, directories, file_names in os.walk(path):
        for file_name in file_names: 
            file_path = os.path.join(root, file_name)
            if file_path.lower().endswith(FILE_EXTENSION):
                with open(file_path, encoding="utf-8") as f:
                    content = f.read()
                    yield (file_name, content)

def find_urls(rst_content):
    lines = enumerate(rst_content.splitlines(), 1)
    return ((line_number, found_url)
            for line_number, line_content in lines
            for found_url in re.findall(REGEX, line_content))

def find_invalid_urls(rst_content):
    return ((line_number, found_url)
            for line_number, found_url in find_urls(rst_content)
            if not is_valid_url(found_url))

def report(path):
    found_invalid_urls = [(file_name, line_number, found_url)
                          for file_name, content in explore(path)
                          for line_number, found_url in find_invalid_urls(content)]
    if found_invalid_urls:
        logging.warning('There are invalid urls.')
        for file_name, line_number, url in found_invalid_urls:
            print('%s\t%s\t%s' % (file_name, line_number, url))
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
