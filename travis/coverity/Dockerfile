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

FROM ubuntu:16.04
RUN apt-get update
RUN apt-get install -y python3-pip git build-essential cmake libmbedtls-dev \
    libssl-dev python3 libpython3-dev wget python3-cryptography \
    python3-sphinx python3-requests valgrind curl <CC_PKG> <CXX_PKG>
RUN sed -i -e "s/-Wdate-time/ /g" \
    /usr/lib/python3.5/config-3.5m-x86_64-linux-gnu/Makefile \
    /usr/lib/python3.5/plat-x86_64-linux-gnu/_sysconfigdata_m.py
RUN wget https://scan.coverity.com/download/linux64 --post-data "token=<COVERITY_SCAN_TOKEN>&project=AVSystem%2FAnjay" -O coverity_tool.tgz \
    && tar xf coverity_tool.tgz
COPY . /Anjay
CMD cd Anjay && ./devconfig <DEVCONFIG_FLAGS> && CC=$PYMBEDTLS_CC; \
    ../cov-analysis*/bin/cov-build --dir cov-int make \
    && tar zcf cov-int.tgz cov-int \
    && curl --form token=<COVERITY_SCAN_TOKEN> \
            --form email=<COVERITY_EMAIL> \
            --form file=@cov-int.tgz \
            --form version=$(git rev-parse HEAD) \
            --form description="" \
            https://scan.coverity.com/builds?project=AVSystem%2FAnjay
