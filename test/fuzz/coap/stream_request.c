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
#include <assert.h>

#include <avsystem/commons/socket_v_table.h>

#include "../../../src/coap/stream.h"

typedef struct mock_socket {
    const avs_net_socket_v_table_t *const vtable;
    uint16_t last_msg_id;
} mock_socket_t;

static int success() { return 0; }

static int mock_recv(avs_net_abstract_socket_t *socket,
                     size_t *out_bytes_received,
                     void *buffer,
                     size_t buffer_length) {
    (void)socket;
    assert(buffer_length >= 4);

    //      version
    //      |  type = ACKNOWLEDGEMENT
    //      |  |  token length
    //      v  v  v     .- code .
    //      01 10 0000  000 00000
    // hex:     6    0      0   0
#define EMPTY_ACK "\x60\x00"

    *out_bytes_received = 4;
    memcpy(buffer, EMPTY_ACK, sizeof(EMPTY_ACK) - 1);
    uint16_t id = ((mock_socket_t*)socket)->last_msg_id;
    memcpy((char*)buffer + sizeof(EMPTY_ACK) - 1, &id, sizeof(id));
    return 0;
}

static int mock_send(avs_net_abstract_socket_t *socket,
                     const void *buffer,
                     size_t size) {
    assert(size >= 4);
    memcpy(&((mock_socket_t*)socket)->last_msg_id,
           (const char*)buffer + 2,
           sizeof(uint16_t));
    return 0;
}

static const avs_net_socket_v_table_t MOCK_SOCKET_VTABLE = {
    .receive = mock_recv,
    .send = mock_send,

    .accept             = (avs_net_socket_accept_t)          success,
    .bind               = (avs_net_socket_bind_t)            success,
    .cleanup            = (avs_net_socket_cleanup_t)         success,
    .close              = (avs_net_socket_close_t)           success,
    .connect            = (avs_net_socket_connect_t)         success,
    .decorate           = (avs_net_socket_decorate_t)        success,
    .get_interface_name = (avs_net_socket_get_interface_t)   success,
    .get_local_port     = (avs_net_socket_get_local_port_t)  success,
    .get_opt            = (avs_net_socket_get_opt_t)         success,
    .get_remote_host    = (avs_net_socket_get_remote_host_t) success,
    .get_remote_port    = (avs_net_socket_get_remote_port_t) success,
    .get_system_socket  = (avs_net_socket_get_system_t)      success,
    .receive_from       = (avs_net_socket_receive_from_t)    success,
    .send_to            = (avs_net_socket_send_to_t)         success,
    .set_opt            = (avs_net_socket_set_opt_t)         success,
    .shutdown           = (avs_net_socket_shutdown_t)        success,
};

mock_socket_t mock_socket_struct = {
    .vtable = &MOCK_SOCKET_VTABLE,
    .last_msg_id = 0
};

enum {
    OP_SETUP_REQUEST,
    OP_WRITE,
    OP_FINISH_MESSAGE,
    OP_RESET,
    OP_SET_ERROR,
};

static void perform_op(FILE *cmd_stream,
                       avs_stream_abstract_t *stream) {
    char cmd;
    if (fread(&cmd, 1, 1, cmd_stream) != 1) {
        return;
    }

    switch (cmd) {
    case OP_SETUP_REQUEST:
        {
            anjay_msg_details_t details;
            uint8_t token_size;
            anjay_coap_token_t token;

            memset(&details, 0, sizeof(details));

            if (fread(&details,
                      sizeof(details.msg_type) + sizeof(details.msg_code)
                      + sizeof(details.format) + sizeof(details.observe_serial),
                      1, cmd_stream) != 1
                    || details.msg_type < _ANJAY_COAP_MSG_FIRST
                    || details.msg_type > _ANJAY_COAP_MSG_LAST
                    || fread(&token_size, 1, 1, cmd_stream) != 1
                    || token_size > 8
                    || (token_size > 0 && fread(&token, token_size, 1, cmd_stream) != 1)) {
                return;
            }

            _anjay_coap_stream_setup_request(stream, &details,
                                             &token, token_size);
        }
    case OP_WRITE:
        {
            uint16_t size;
            char buffer[UINT16_MAX];

            if (fread(&size, sizeof(size), 1, cmd_stream) != 1
                    || fread(buffer, size, 1, cmd_stream) != 1) {
                return;
            }

            avs_stream_write(stream, buffer, (size_t)size);
            return;
        }
    case OP_FINISH_MESSAGE:
        avs_stream_finish_message(stream);
        return;
    case OP_RESET:
        avs_stream_reset(stream);
        return;
    case OP_SET_ERROR:
        {
            uint8_t code;
            if (fread(&code, sizeof(code), 1, cmd_stream) != 1) {
                return;
            }

            _anjay_coap_stream_set_error(stream, code);
        }
    default:
        return;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    avs_net_abstract_socket_t *mock_socket =
            (avs_net_abstract_socket_t*)&mock_socket_struct;
    anjay_coap_socket_t *sock;
    avs_stream_abstract_t *stream = NULL;

    int retval = -1;

    if (_anjay_coap_socket_create(&sock, mock_socket)
            || _anjay_coap_stream_create(&stream, sock,
                                         UINT16_MAX + 1, UINT16_MAX + 1)) {
        goto exit;
    }

    while (!feof(stdin)) {
        perform_op(stdin, stream);

    }

    retval = 0;

exit:
    avs_stream_cleanup(&stream);
    return retval;
}
