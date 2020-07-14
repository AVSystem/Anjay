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
import os
import yaml
import subprocess
import contextlib
import logging


def parse_configurations(travis_yml_filename):
    with open(travis_yml_filename, 'r') as f:
        parsed = yaml.safe_load(f)

    configurations = []
    for stage in parsed['matrix']['include']:
        if (stage['os'], stage['stage']) != ('linux', 'test'):
            continue
        configurations += [stage]

    return configurations


@contextlib.contextmanager
def scoped_chdir(path):
    prev_dir = os.getcwd()

    try:
        os.chdir(path)
        yield
    finally:
        os.chdir(prev_dir)


def run_configuration(root_dir, configuration):
    run_script = os.path.join(root_dir, 'travis/run.py')

    with scoped_chdir(root_dir):
        logging.info('Running configuration: %s' % (configuration['env'],))
        run_env = configuration['env'] + \
            ' CHECK_COMMAND="make -j && make -j anjay_check avs_commons_check avs_coap_check"'
        try:
            subprocess.run('{env} {run}'.format(env=run_env, run=run_script),
                           shell=True, check=True, env={'NO_CACHE': '1'})
        except:
            logging.error('Failed configuration %s' % (configuration['env'],))
            raise


if __name__ == '__main__':
    import argparse
    import subprocess

    parser = argparse.ArgumentParser('Parses .travis.yml, extracting configurations, and then runs the travis images to check if they successfully compile.')
    parser.add_argument('-r', '--root-dir', type=str, help='Root directory of Anjay repo',
                        required=True)
    args = parser.parse_args()

    for configuration in parse_configurations(os.path.join(args.root_dir, '.travis.yml')):
        run_configuration(args.root_dir, configuration)

