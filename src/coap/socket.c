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

#include <config.h>

#include "socket.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

#include <avsystem/commons/list.h>

#include "log.h"

VISIBILITY_SOURCE_BEGIN

struct anjay_coap_socket {
    avs_net_abstract_socket_t *dtls_socket;
};

int _anjay_coap_socket_create(anjay_coap_socket_t **sock,
                              avs_net_abstract_socket_t *backend) {
    *sock = (anjay_coap_socket_t*)calloc(1, sizeof(anjay_coap_socket_t));
    if (!*sock) {
        return -1;
    }
    (*sock)->dtls_socket = backend;
    return 0;
}

int _anjay_coap_socket_close(anjay_coap_socket_t *sock) {
    assert(sock);
    if (!sock->dtls_socket) {
        return 0;
    }
    return avs_net_socket_close(sock->dtls_socket);
}

void _anjay_coap_socket_cleanup(anjay_coap_socket_t **sock) {
    if (!sock || !*sock) {
        return;
    }

    _anjay_coap_socket_close(*sock);
    avs_net_socket_cleanup(&(*sock)->dtls_socket);
    free(*sock);
    *sock = NULL;
}

int _anjay_coap_socket_send(anjay_coap_socket_t *sock,
                            const anjay_coap_msg_t *msg) {
    assert(sock && sock->dtls_socket);
    if (!_anjay_coap_msg_is_valid(msg)) {
        coap_log(ERROR, "cannot send an invalid CoAP message\n");
        return -1;
    }

    coap_log(TRACE, "send: %s", ANJAY_COAP_MSG_SUMMARY(msg));
    return avs_net_socket_send(sock->dtls_socket, &msg->header, msg->length);
}

int _anjay_coap_socket_recv(anjay_coap_socket_t *sock,
                            anjay_coap_msg_t *out_msg,
                            size_t msg_capacity) {
    assert(sock && sock->dtls_socket);
    assert(msg_capacity < UINT32_MAX);

    size_t msg_length = 0;
    int result = 0;

    if (avs_net_socket_receive(sock->dtls_socket, &msg_length, &out_msg->header,
                               msg_capacity - sizeof(out_msg->length))) {
        int error = avs_net_socket_errno(sock->dtls_socket);
        coap_log(ERROR, "recv failed: errno = %d", error);
        if (error == ETIMEDOUT) {
            result = ANJAY_COAP_SOCKET_RECV_ERR_TIMEOUT;
        } else if (error == EMSGSIZE) {
            result = ANJAY_COAP_SOCKET_RECV_ERR_MSG_TOO_LONG;
        } else {
            result = ANJAY_COAP_SOCKET_RECV_ERR_OTHER;
        }
    }
    out_msg->length = (uint32_t)msg_length;

    if (result) {
        return result;
    }

    if (_anjay_coap_msg_is_valid(out_msg)) {
        coap_log(TRACE, "recv: %s", ANJAY_COAP_MSG_SUMMARY(out_msg));
        return 0;
    } else {
        coap_log(DEBUG, "recv: malformed message");
        return ANJAY_COAP_SOCKET_RECV_ERR_MSG_MALFORMED;
    }
}

int _anjay_coap_socket_get_recv_timeout(anjay_coap_socket_t *sock) {
    avs_net_socket_opt_value_t value;

    if (avs_net_socket_get_opt(sock->dtls_socket,
                               AVS_NET_SOCKET_OPT_RECV_TIMEOUT, &value)) {
        assert(0 && "should never happen");
        coap_log(ERROR, "could not get socket recv timeout");
        return 0;
    }

    return value.recv_timeout;
}

void _anjay_coap_socket_set_recv_timeout(anjay_coap_socket_t *sock,
                                         int timeout_ms) {
    avs_net_socket_opt_value_t value = {
        .recv_timeout = timeout_ms
    };

    if (avs_net_socket_set_opt(sock->dtls_socket,
                               AVS_NET_SOCKET_OPT_RECV_TIMEOUT, value)) {
        assert(0 && "should never happen");
        coap_log(ERROR, "could not set socket recv timeout");
    }
}

avs_net_abstract_socket_t *
_anjay_coap_socket_get_backend(anjay_coap_socket_t *sock) {
    return sock->dtls_socket;
}

void _anjay_coap_socket_set_backend(anjay_coap_socket_t *sock,
                                    avs_net_abstract_socket_t *backend) {
    sock->dtls_socket = backend;
}

#if defined(ANJAY_TEST) || defined(ANJAY_FUZZ_TEST)
#endif // ANJAY_TEST || ANJAY_FUZZ_TEST

#ifdef ANJAY_TEST
#include "test/socket.c"
#endif // ANJAY_TEST
