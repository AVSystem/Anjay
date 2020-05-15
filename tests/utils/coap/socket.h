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
