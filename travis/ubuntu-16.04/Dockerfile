# Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

FROM avsystemembedded/anjay-travis:ubuntu-16.04
RUN apt-get update
RUN apt-get -y install <ANJAY_CC> <ANJAY_CXX>
COPY . /Anjay
CMD cd Anjay && ./devconfig --with-valgrind <DEVCONFIG_FLAGS> && CC=gcc; export LC_ALL=C.UTF-8; <CHECK_COMMAND>
