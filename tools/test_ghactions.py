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
import copy
import logging
import multiprocessing
import os
import re
import subprocess
import tempfile

import yaml


def apply_substitution(data, pattern, repl):
    if isinstance(data, dict):
        keys = data.keys()
    else:
        keys = range(len(data))
    for key in keys:
        if isinstance(data[key], str):
            data[key] = re.sub(pattern, repl, data[key])
        else:
            apply_substitution(data[key], pattern, repl)


def enumerate_jobs(yaml):
    jobs = yaml.get('jobs') or {}
    for job_name, job in jobs.items():
        if 'strategy' not in job:
            # just a single job
            yield {'name': job_name, **job}
            continue

        # jobs with no containers/images (e.g. 'macos-*' jobs) are not supported
        if 'container' not in job:
            continue

        # fail-fast option is not supported
        if 'fail-fast' in job['strategy']:
            del job['strategy']['fail-fast']

        if job['strategy'].keys() != {'matrix'} or job['strategy']['matrix'].keys() != {'include'}:
            raise NotImplementedError('Unsupported strategy configuration')

        for matrix_entry in job['strategy']['matrix']['include']:
            copied_job = copy.deepcopy(job)
            del copied_job['strategy']

            for sub_key in matrix_entry.keys():
                apply_substitution(copied_job, r'\${{ *matrix\.' + sub_key + r' *}}',
                                   matrix_entry[sub_key])
            yield {'name': '%s %r' % (job_name, matrix_entry), **copied_job}


def validate_job(job):
    if not set(job.keys()).issubset({'container', 'env', 'name', 'runs-on', 'steps'}):
        raise NotImplementedError('Unsupported job features used')
    if not 'container' in job:
        raise NotImplementedError('Container not specified')
    checkout_steps = 0
    for step in job['steps']:
        if step == {'uses': 'actions/checkout@v1', 'with': {'submodules': 'recursive'}}:
            checkout_steps += 1
        elif step.keys() != {'run'}:
            raise NotImplementedError('Unsupported step type')
    if checkout_steps != 1:
        raise NotImplementedError('There needs to be a single checkout step')


def run_job(job, root_dir):
    DOCKER_ROOT_DIR = '/test_ghactions_root'
    DOCKER_SCRIPT_DIR = '/test_ghactions_script'

    validate_job(job)
    with tempfile.TemporaryDirectory() as temp_dir:
        with open(os.path.join(temp_dir, 'run.sh'), 'w') as script:
            script.write('#!/bin/sh\n')
            script.write('set -e\n')
            script.write('TEMP_DIR="$(mktemp -d)"\n')
            script.write('cp -a ' + DOCKER_ROOT_DIR + ' "$TEMP_DIR"\n')
            script.write('cd "$TEMP_DIR"/' + os.path.basename(DOCKER_ROOT_DIR) + '\n')
            for step in job['steps']:
                if step.keys() == {'run'}:
                    script.write(step['run'] + '\n')
            script.flush()
            os.chmod(os.path.join(temp_dir, 'run.sh'), 0o777)

            command = [
                'docker', 'run', '--rm', '--mount',
                'type=bind,source=' + os.path.realpath(root_dir) + ',target=' + DOCKER_ROOT_DIR,
                '--mount', 'type=bind,source=' + temp_dir + ',target=' + DOCKER_SCRIPT_DIR]
            env = job.get('env') or {}
            for env_key, env_value in env.items():
                command += ['--env', env_key + '=' + env_value]
            command.append(job['container'])
            command.append(DOCKER_SCRIPT_DIR + '/run.sh')

        logging.log(logging.INFO, 'Running %s: %r', job['name'], command)
        subprocess.check_call(command)


def _main():
    parser = argparse.ArgumentParser(
        'Parses .github/workflows/anjay-tests.yml, extracting configurations, and then runs them to check if they successfully compile.')
    parser.add_argument('-r', '--root-dir', type=str, help='Root directory of Anjay repo',
                        required=True)
    parser.add_argument('--substitution', type=str, help='Replace certain commands in jobs',
                        default='make check=make -j%d anjay_check avs_commons_check avs_coap_check examples' % multiprocessing.cpu_count())
    args = parser.parse_args()

    with open(os.path.join(args.root_dir, '.github', 'workflows', 'anjay-tests.yml'), 'r') as f:
        parsed = yaml.safe_load(f)

    logging.basicConfig(level=logging.NOTSET)
    jobs = list(enumerate_jobs(parsed))

    if args.substitution != '':
        for job in jobs:
            apply_substitution(job, *args.substitution.split('=', 1))
    for job in jobs:
        run_job(job, args.root_dir)


if __name__ == '__main__':
    _main()
