/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANJAY_TEST_DOWNLOADER_MOCK_H
#define ANJAY_TEST_DOWNLOADER_MOCK_H

#include <avsystem/commons/avs_unit_mock_helpers.h>

AVS_UNIT_MOCK_CREATE(avs_net_tcp_socket_create)
#define avs_net_tcp_socket_create(...) \
    AVS_UNIT_MOCK_WRAPPER(avs_net_tcp_socket_create)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(avs_net_udp_socket_create)
#define avs_net_udp_socket_create(...) \
    AVS_UNIT_MOCK_WRAPPER(avs_net_udp_socket_create)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(avs_net_ssl_socket_create)
#define avs_net_ssl_socket_create(...) \
    AVS_UNIT_MOCK_WRAPPER(avs_net_ssl_socket_create)(__VA_ARGS__)

AVS_UNIT_MOCK_CREATE(avs_net_dtls_socket_create)
#define avs_net_dtls_socket_create(...) \
    AVS_UNIT_MOCK_WRAPPER(avs_net_dtls_socket_create)(__VA_ARGS__)

#endif /* ANJAY_TEST_DOWNLOADER_MOCK_H */
