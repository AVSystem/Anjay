#!/usr/bin/env python3
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
import re
import subprocess
import tempfile
import uuid

assert __name__ == '__main__'

check_command = os.getenv('CHECK_COMMAND', 'make check')
os.environ['CHECK_COMMAND'] = check_command

no_cache = os.getenv('NO_CACHE', None)

docker_image = os.getenv('DOCKER_IMAGE', '')
if docker_image != '':
    with open('travis/%s/Dockerfile' % (docker_image,), 'r') as f:
        dockerfile = f.read()

    dockerfile = re.sub('<([a-zA-Z0-9_]*)>', lambda match: os.getenv(match.group(1), ''),
                        dockerfile)

    with tempfile.TemporaryDirectory() as temp_dir:
        dockerfile_path = '%s/Dockerfile-%s' % (temp_dir, docker_image)
        with open(dockerfile_path, 'w') as f:
            f.write(dockerfile)

        image_uuid = str(uuid.uuid4()).lower()
        image_name = '%s-%s' % (docker_image, image_uuid)

        args = ['docker', 'build', '--tag', image_name, '--file', dockerfile_path ]
        if no_cache is not None:
            args += ['--no-cache']

        subprocess.check_call(args + ['.'])

        try:
            subprocess.check_call(['docker', 'run',
                                   '-e', 'CC=%s' % (os.getenv('ANJAY_CC', ''),),
                                   '-e', 'CXX=%s' % (os.getenv('ANJAY_CXX', ''),),
                                   image_name])
        finally:
            subprocess.check_call(['docker', 'rmi', '-f', image_name])
else:
    subprocess.check_call(
        './devconfig %s && make -j && %s' % (
            os.getenv('DEVCONFIG_FLAGS', ''), check_command),
        shell=True)
