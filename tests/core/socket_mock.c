/*
 * Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include "socket_mock.h"

AVS_UNIT_MOCK_DEFINE(avs_net_tcp_socket_create)
AVS_UNIT_MOCK_DEFINE(avs_net_udp_socket_create)
AVS_UNIT_MOCK_DEFINE(avs_net_ssl_socket_create)
AVS_UNIT_MOCK_DEFINE(avs_net_dtls_socket_create)
