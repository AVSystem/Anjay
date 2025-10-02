# Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
# AVSystem CoAP library
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

cmake_minimum_required(VERSION 3.16)
project(async-client C)

find_package(avs_coap REQUIRED)

add_executable(async-client src/main.c)
target_link_libraries(async-client PRIVATE avs_coap)
