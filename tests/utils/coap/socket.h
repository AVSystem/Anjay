/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_TEST_COAP_SOCKET_H
#define ANJAY_TEST_COAP_SOCKET_H

#include <avsystem/commons/avs_unit_mocksock.h>

/**
 * NOTE: inner_mtu / mtu may be set to a negative value, in which case
 * they are not automatically handled by mocksock_get_opt()
 */
void _anjay_mocksock_create(avs_net_socket_t **mocksock,
                            int inner_mtu,
                            int mtu);

#endif /* ANJAY_TEST_COAP_SOCKET_H */
