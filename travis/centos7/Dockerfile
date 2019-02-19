# Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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
# required for mbedtls-devel and python3.5
RUN yum install -y https://centos7.iuscommunity.org/ius-release.rpm
RUN yum install -y which git make cmake3 boost-python-devel mbedtls-devel openssl openssl-devel python-sphinx python-sphinx_rtd_theme valgrind valgrind-devel gcc gcc-c++
RUN ln -s /usr/bin/cmake3 /usr/bin/cmake
# required to compile pybind11
RUN yum install -y python-tools
RUN yum install -y python35u python35u-devel python35u-pip
RUN python3.5 -m pip install cryptography requests jinja2
# older centos7 images do not have python3
# some newer ones make it resolve to python3.4 and we need 3.5+
RUN ln -sf /usr/bin/python3.5 /usr/bin/python3
COPY . /Anjay
CMD cd Anjay && ./devconfig --with-valgrind <DEVCONFIG_FLAGS> && make -j && cd test/integration && make integration_check
