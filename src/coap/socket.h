/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_COAP_SOCKET_H
#define ANJAY_COAP_SOCKET_H

#include <anjay/anjay.h>

#include "msg.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct anjay_coap_socket anjay_coap_socket_t;

int _anjay_coap_socket_create(anjay_coap_socket_t **sock,
                              avs_net_abstract_socket_t *backend);

int _anjay_coap_socket_close(anjay_coap_socket_t *sock);

void _anjay_coap_socket_cleanup(anjay_coap_socket_t **sock);

int _anjay_coap_socket_send(anjay_coap_socket_t *sock,
                            const anjay_coap_msg_t *msg);

#define ANJAY_COAP_SOCKET_RECV_ERR_TIMEOUT -1
#define ANJAY_COAP_SOCKET_RECV_ERR_MSG_MALFORMED -2
#define ANJAY_COAP_SOCKET_RECV_ERR_OTHER -3
#define ANJAY_COAP_SOCKET_RECV_ERR_MSG_TOO_LONG -4

/**
 * @returns 0 on success, a negative value in case of error:
 * - ANJAY_COAP_SOCKET_RECV_ERR_TIMEOUT if the socket timeout expired, but no
 *   message was received
 * - ANJAY_COAP_SOCKET_RECV_ERR_MSG_MALFORMED when a packet was successfully
 *   received, but it was not a correct CoAP message
 * - ANJAY_COAP_SOCKET_RECV_ERR_OTHER in case of other error
 **/
int _anjay_coap_socket_recv(anjay_coap_socket_t *sock,
                            anjay_coap_msg_t *out_msg,
                            size_t msg_capacity);

int _anjay_coap_socket_get_recv_timeout(anjay_coap_socket_t *sock);
void _anjay_coap_socket_set_recv_timeout(anjay_coap_socket_t *sock,
                                         int timeout_ms);

avs_net_abstract_socket_t *
_anjay_coap_socket_get_backend(anjay_coap_socket_t *sock);

void _anjay_coap_socket_set_backend(anjay_coap_socket_t *sock,
                                    avs_net_abstract_socket_t *backend);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_SOCKET_H
