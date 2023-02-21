# Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
# AVSystem CoAP library
# All rights reserved.
#
# Licensed under the AVSystem-5-clause License.
# See the attached LICENSE file for details.

cmake_minimum_required(VERSION 3.1)
project(async-client C)

find_package(avs_coap REQUIRED)

add_executable(async-client src/main.c)
target_link_libraries(async-client PRIVATE avs_coap)
