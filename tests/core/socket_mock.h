/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#ifndef SOCKET_MOCK_H
#define SOCKET_MOCK_H

#include <avsystem/commons/avs_net.h>
#include <avsystem/commons/avs_unit_mock_helpers.h>

extern AVS_UNIT_MOCK_DECLARE(avs_net_tcp_socket_create);
#define avs_net_tcp_socket_create(...) \
    AVS_UNIT_MOCK_WRAPPER(avs_net_tcp_socket_create)(__VA_ARGS__)

extern AVS_UNIT_MOCK_DECLARE(avs_net_udp_socket_create);
#define avs_net_udp_socket_create(...) \
    AVS_UNIT_MOCK_WRAPPER(avs_net_udp_socket_create)(__VA_ARGS__)

extern AVS_UNIT_MOCK_DECLARE(avs_net_ssl_socket_create);
#define avs_net_ssl_socket_create(...) \
    AVS_UNIT_MOCK_WRAPPER(avs_net_ssl_socket_create)(__VA_ARGS__)

extern AVS_UNIT_MOCK_DECLARE(avs_net_dtls_socket_create);
#define avs_net_dtls_socket_create(...) \
    AVS_UNIT_MOCK_WRAPPER(avs_net_dtls_socket_create)(__VA_ARGS__)

#endif /* SOCKET_MOCK_H */
