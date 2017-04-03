# Copyright 2017 AVSystem <avsystem@avsystem.com>
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
RUN yum install -y which git make cmake3 boost-python-devel mbedtls-devel openssl openssl-devel wget python-sphinx python-sphinx_rtd_theme valgrind valgrind-devel gcc gcc-c++
# required to compile boost::python
RUN yum install -y boost-jam python-tools
RUN yum install -y python35u python35u-devel
RUN yum install -y http://pubrepo.maxiv.lu.se/rpm/el7/x86_64/boost-python3-1.53.0-25.el7.x86_64.rpm
RUN rpm -ivh --nodeps http://pubrepo.maxiv.lu.se/rpm/el7/x86_64/boost-python3-devel-1.53.0-25.el7.x86_64.rpm
RUN ln -s /usr/bin/cmake3 /usr/bin/cmake
RUN ln -s /usr/bin/python3.5 /usr/bin/python3
COPY . /Anjay
CMD cd Anjay && ./devconfig --with-valgrind <DEVCONFIG_FLAGS> && make -j && cd test/integration && make integration_check
