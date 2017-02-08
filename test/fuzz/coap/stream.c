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

#include <stdint.h>
#include <stdio.h>

#include <avsystem/commons/socket_v_table.h>

#include "../../../src/coap/stream.h"

static int success() { return 0; }
static int fail() { return -1; }

static int stdin_recv(avs_net_abstract_socket_t *socket,
                      size_t *out_bytes_received,
                      void *buffer,
                      size_t buffer_length) {
    (void)socket;

    size_t bytes_read = fread(buffer, 1, buffer_length, stdin);
    if (bytes_read == 0 || bytes_read >= buffer_length) {
        return -1;
    }

    *out_bytes_received = bytes_read;
    return 0;
}

static int stdin_get_opt(avs_net_abstract_socket_t *socket,
                         avs_net_socket_opt_key_t opt,
                         avs_net_socket_opt_value_t *value) {
    (void)socket;
    (void)opt;
    if (opt == AVS_NET_SOCKET_OPT_RECV_TIMEOUT) {
        value->recv_timeout = 1;
    } else {
        memset(value, 0, sizeof(*value));
    }
    return 0;
}

static const avs_net_socket_v_table_t STDIN_SOCKET_VTABLE = {
    .receive = stdin_recv,

    .connect = (avs_net_socket_connect_t)success,
    .bind = (avs_net_socket_bind_t)success,
    .close = (avs_net_socket_close_t)success,
    .cleanup = (avs_net_socket_cleanup_t)success,

    .decorate = (avs_net_socket_decorate_t)fail,
    .send = (avs_net_socket_send_t)fail,
    .send_to = (avs_net_socket_send_to_t)fail,
    .receive_from = (avs_net_socket_receive_from_t)fail,
    .accept = (avs_net_socket_accept_t)fail,
    .shutdown = (avs_net_socket_shutdown_t)fail,
    .get_system_socket = (avs_net_socket_get_system_t)fail,
    .get_interface_name = (avs_net_socket_get_interface_t)fail,
    .get_remote_host = (avs_net_socket_get_remote_host_t)fail,
    .get_remote_port = (avs_net_socket_get_remote_port_t)fail,
    .get_local_port = (avs_net_socket_get_local_port_t)fail,
    .get_opt = stdin_get_opt,
    .set_opt = (avs_net_socket_set_opt_t)success,
};

struct stdin_socket {
    const avs_net_socket_v_table_t *const vtable;
} stdin_socket_struct = {
    &STDIN_SOCKET_VTABLE
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    avs_net_abstract_socket_t *stdin_socket =
            (avs_net_abstract_socket_t*)&stdin_socket_struct;
    anjay_coap_socket_t *sock;
    avs_stream_abstract_t *stream = NULL;

    char buffer[UINT16_MAX + 1];
    size_t bytes_read;

    int retval = 0;
    char message_finished = 0;

    if (_anjay_coap_socket_create(&sock, stdin_socket)
            || _anjay_coap_stream_create(&stream, sock,
                                         UINT16_MAX + 1, UINT16_MAX + 1)
            || avs_stream_read(stream, &bytes_read, &message_finished,
                               buffer, sizeof(buffer))) {
        retval = -1;
    }

    avs_stream_cleanup(&stream);
    return retval;
}
