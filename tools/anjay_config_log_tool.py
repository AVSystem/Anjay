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
import argparse
import enum
import os
import re
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))

ConfigFileVariableType = enum.Enum('ConfigFileVariableType', 'VALUE FLAG')


def default_config_files(project_root=PROJECT_ROOT):
    return [os.path.join(project_root, 'deps', 'avs_commons', 'include_public',
                         'avsystem', 'commons', 'avs_commons_config.h.in'),
            os.path.join(project_root, 'deps', 'avs_coap', 'include_public',
                         'avsystem', 'coap', 'avs_coap_config.h.in'),
            os.path.join(project_root, 'include_public', 'anjay', 'anjay_config.h.in')]


def default_config_log_file(project_root=PROJECT_ROOT):
    return os.path.join(project_root, 'src', 'anjay_config_log.h')


def enumerate_variables(config_files):
    result = {}
    origins = {}

    def emit(config_file, name, value):
        if name in result:
            assert name in origins
            raise ValueError(
                'Variable %s from %s duplicates one from %s' % (name, config_file, origins[name]))
        result[name] = value
        origins[name] = config_file

    for config_file in config_files:
        with open(config_file) as f:
            for line in f:
                stripped = line.strip()
                match = re.match(r'#[ \t]*define[ \t]+([A-Za-z_0-9]+)[ \t]+@([A-Za-z_0-9]+)@',
                                 stripped)
                if match:
                    emit(config_file, match.group(1), ConfigFileVariableType.VALUE)
                    continue

                match = re.match(r'#[ \t]*cmakedefine[ \t]+([A-Za-z_0-9]+)', stripped)
                if match:
                    emit(config_file, match.group(1), ConfigFileVariableType.FLAG)
                    continue

                if any(re.search(pattern, stripped) for pattern in
                       (r'^[ \t]*#[ \t]*cmakedefine', r'^[ \t]*#.*@[A-Za-z_0-9]+@')):
                    raise ValueError('Found unloggable line in %s: %s' % (config_file, stripped))

    return result


def _generate_body(variables):
    lines = []
    for name, type in sorted(variables.items()):
        if type == ConfigFileVariableType.VALUE:
            lines.append(
                '    _anjay_log(anjay, TRACE, "%s = " AVS_QUOTE_MACRO(%s));' % (name, name))
        else:
            lines.append('#ifdef ' + name)
            lines.append('    _anjay_log(anjay, TRACE, "%s = ON");' % (name,))
            lines.append('#else // ' + name)
            lines.append('    _anjay_log(anjay, TRACE, "%s = OFF");' % (name,))
            lines.append('#endif // ' + name)
    return '\n'.join(lines)


def _split_config_log_file(config_log_file):
    def find_only(haystack, needle):
        pos = haystack.find(needle)
        if pos < 0:
            raise ValueError("Could not find '%s' in %s" % (needle, config_log_file))
        other = haystack.find(needle, pos + 1)
        if other >= 0:
            raise ValueError("Found '%s' more than once in %s" % (needle, config_log_file))
        return pos

    with open(config_log_file) as f:
        config_log = f.read()

    lbracket = find_only(config_log, '{')
    rbracket = find_only(config_log, '}')
    return config_log[:lbracket + 1], config_log[lbracket + 1:rbracket], config_log[rbracket:]


def validate(config_log_file, config_files):
    header, contents, footer = _split_config_log_file(config_log_file)
    expected_contents = _generate_body(enumerate_variables(config_files))
    if contents.strip() != expected_contents.strip():
        raise ValueError('%s is outdated. Please regenerate it by calling "%s update"' % (
            config_log_file, __file__))


def update(config_log_file, config_files):
    header, contents, footer = _split_config_log_file(config_log_file)
    new_contents = _generate_body(enumerate_variables(config_files))
    with open(config_log_file, 'w') as f:
        f.write(header)
        f.write('\n')
        f.write(new_contents)
        f.write('\n')
        f.write(footer)


def list_flags(config_files):
    for name, type in sorted(enumerate_variables(config_files).items()):
        if type == ConfigFileVariableType.FLAG:
            print(name)


def _main():
    parser = argparse.ArgumentParser(description='Validate or update anjay_config_log.h file.',
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('command', choices=['validate', 'update', 'list_flags'])
    parser.add_argument('-c', '--config-files',
                        help='%r-separated list of *.h.in files to enumerate variables from'
                             % (os.pathsep,),
                        default=default_config_files())
    parser.add_argument('-l', '--config-log-file', help='anjay_config_log.h file to operate on',
                        default=default_config_log_file())
    args = parser.parse_args()

    config_files = args.config_files
    if isinstance(config_files, str):
        config_files = config_files.split(os.pathsep)

    if args.command == 'validate':
        return validate(args.config_log_file, config_files)
    elif args.command == 'update':
        return update(args.config_log_file, config_files)
    elif args.command == 'list_flags':
        return list_flags(config_files)
    else:
        raise RuntimeError('Invalid command')


if __name__ == '__main__':
    sys.exit(_main())
