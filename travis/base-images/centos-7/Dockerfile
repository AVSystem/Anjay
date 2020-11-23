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

FROM centos:7
# Work around issue described in https://sourceware.org/ml/libc-alpha/2014-07/msg00200.html
COPY ./valgrind.supp /tmp/valgrind.supp
# required for mbedtls-devel and python3.5
RUN yum install -y https://repo.ius.io/ius-release-el7.rpm https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
RUN yum install -y which git make cmake3 boost-python-devel mbedtls-devel openssl openssl-devel valgrind valgrind-devel gcc gcc-c++ zlib-devel
RUN ln -s /usr/bin/cmake3 /usr/bin/cmake
# required to compile pybind11
RUN yum install -y python-tools
RUN yum install -y python35u python35u-devel python35u-pip
RUN python3.5 -m pip install cryptography requests jinja2 sphinx sphinx-rtd-theme
# older centos7 images do not have python3
# some newer ones make it resolve to python3.4 and we need 3.5+
RUN ln -sf /usr/bin/python3.5 /usr/bin/python3
RUN cat /tmp/valgrind.supp >> /usr/lib64/valgrind/default.supp
