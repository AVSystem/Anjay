/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
