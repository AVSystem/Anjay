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
#include "msg_cache.h"

#define ANJAY_COAP_STREAM_INTERNALS
#include "stream/common.h"

VISIBILITY_SOURCE_BEGIN

struct anjay_coap_socket {
    avs_net_abstract_socket_t *dtls_socket;

    const coap_transmission_params_t *tx_params;
    coap_msg_cache_t *msg_cache;
};

int _anjay_coap_socket_create(anjay_coap_socket_t **sock,
                              avs_net_abstract_socket_t *backend,
                              size_t msg_cache_size) {
    *sock = (anjay_coap_socket_t *) calloc(1, sizeof(anjay_coap_socket_t));
    if (!*sock) {
        return -1;
    }

    if (msg_cache_size > 0) {
        (*sock)->msg_cache = _anjay_coap_msg_cache_create(msg_cache_size);
        if (!(*sock)->msg_cache) {
            coap_log(ERROR, "could not create message cache");
            free(*sock);
            return -1;
        }
    }

    (*sock)->dtls_socket = backend;
    (*sock)->tx_params = &_anjay_coap_DEFAULT_TX_PARAMS;
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

    _anjay_coap_msg_cache_release(&(*sock)->msg_cache);
    _anjay_coap_socket_close(*sock);
    avs_net_socket_cleanup(&(*sock)->dtls_socket);
    free(*sock);
    *sock = NULL;
}

static int map_io_error(avs_net_abstract_socket_t *socket,
                        int result,
                        const char *operation) {
    if (result) {
        int error = avs_net_socket_errno(socket);
        coap_log(ERROR, "%s failed: errno = %d", operation, error);
        if (error == ETIMEDOUT) {
            result = ANJAY_COAP_SOCKET_ERR_TIMEOUT;
        } else if (error == EMSGSIZE) {
            result = ANJAY_COAP_SOCKET_ERR_MSG_TOO_LONG;
        } else {
            result = ANJAY_COAP_SOCKET_ERR_NETWORK;
        }
    }
    return result;
}

#ifndef WITH_MESSAGE_CACHE
#define try_cache_response(...) (void)0
#else // WITH_MESSAGE_CACHE

static int try_cache_response(anjay_coap_socket_t *sock,
                               const anjay_coap_msg_t *res) {
    if (!_anjay_coap_msg_is_response(res) || !sock->msg_cache) {
        return 0;
    }

    char addr[INET6_ADDRSTRLEN];
    char port[sizeof("65535")];
    if (avs_net_socket_get_remote_host(sock->dtls_socket, addr, sizeof(addr))
            || avs_net_socket_get_remote_port(sock->dtls_socket,
                                              port, sizeof(port))) {
        coap_log(DEBUG, "could not get remote remote host/port");
        return -1;
    }

    return _anjay_coap_msg_cache_add(sock->msg_cache, addr, port, res,
                                     sock->tx_params);
}

#endif // WITH_MESSAGE_CACHE


int _anjay_coap_socket_send(anjay_coap_socket_t *sock,
                            const anjay_coap_msg_t *msg) {
    assert(sock && sock->dtls_socket);
    if (!_anjay_coap_msg_is_valid(msg)) {
        coap_log(ERROR, "cannot send an invalid CoAP message\n");
        return -1;
    }

    coap_log(TRACE, "send: %s", ANJAY_COAP_MSG_SUMMARY(msg));
    int result = avs_net_socket_send(sock->dtls_socket,
                                     &msg->header, msg->length);
    if (!result) {
        int cache_result = try_cache_response(sock, msg);
        (void) cache_result;
    }
    return map_io_error(sock->dtls_socket, result, "send");
}

#ifndef WITH_MESSAGE_CACHE
#define try_send_cached_response(...) (-1)
#else // WITH_MESSAGE_CACHE

static int try_send_cached_response(anjay_coap_socket_t *sock,
                                    const anjay_coap_msg_t *req) {
    if (!_anjay_coap_msg_is_request(req) || !sock->msg_cache) {
        return -1;
    }

    char addr[INET6_ADDRSTRLEN];
    char port[sizeof("65535")];
    if (avs_net_socket_get_remote_host(sock->dtls_socket, addr, sizeof(addr))
            || avs_net_socket_get_remote_port(sock->dtls_socket,
                                              port, sizeof(port))) {
        coap_log(DEBUG, "could not get remote remote host/port");
        return -1;
    }

    uint16_t msg_id = _anjay_coap_msg_get_id(req);
    const anjay_coap_msg_t *res = _anjay_coap_msg_cache_get(sock->msg_cache,
                                                            addr, port, msg_id);
    if (res) {
        return _anjay_coap_socket_send(sock, res);
    } else {
        return -1;
    }
}

#endif // WITH_MESSAGE_CACHE

int _anjay_coap_socket_recv(anjay_coap_socket_t *sock,
                            anjay_coap_msg_t *out_msg,
                            size_t msg_capacity) {
    assert(sock && sock->dtls_socket);
    assert(msg_capacity < UINT32_MAX);

    size_t msg_length = 0;
    int result = avs_net_socket_receive(sock->dtls_socket, &msg_length,
                                        &out_msg->header,
                                        msg_capacity - sizeof(out_msg->length));
    out_msg->length = (uint32_t) msg_length;

    if (result) {
        return map_io_error(sock->dtls_socket, result, "receive");
    }

    if (!_anjay_coap_msg_is_valid(out_msg)) {
        coap_log(DEBUG, "recv: malformed message");
        return ANJAY_COAP_SOCKET_ERR_MSG_MALFORMED;
    }

    coap_log(TRACE, "recv: %s", ANJAY_COAP_MSG_SUMMARY(out_msg));
    if (!try_send_cached_response(sock, out_msg)) {
        return ANJAY_COAP_SOCKET_ERR_DUPLICATE;
    }

    return 0;
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

const coap_transmission_params_t *
_anjay_coap_socket_get_tx_params(anjay_coap_socket_t *sock) {
    return sock->tx_params;
}

void
_anjay_coap_socket_set_tx_params(anjay_coap_socket_t *sock,
                                 const coap_transmission_params_t *tx_params) {
    sock->tx_params = tx_params;
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
