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

#include <alloca.h>
#include <netinet/in.h>

#include <avsystem/commons/unit/test.h>
#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream_v_table.h>

#include "../socket.h"
#include "../msg_builder.h"
#include "servers.h"

#define TEST_PORT_DTLS 4321
#define TEST_PORT_UDP 4322

#define COAP_MSG_MAX_SIZE 1152

#define _STR(x) #x
#define STR(x) _STR(x)

AVS_UNIT_TEST(coap_socket, coap_socket) {
    avs_net_socket_opt_value_t mtu;
    { // udp_client_send_recv
        anjay_coap_socket_t *socket =
                _anjay_test_setup_udp_echo_socket(TEST_PORT_UDP);

        anjay_coap_msg_info_t info = _anjay_coap_msg_info_init();
        info.type = ANJAY_COAP_MSG_CONFIRMABLE;
        info.code = ANJAY_COAP_CODE_CONTENT;
        info.identity.msg_id = 4;

        size_t storage_size = COAP_MSG_MAX_SIZE;
        void *storage = malloc(storage_size);

        const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
                _anjay_coap_ensure_aligned_buffer(storage),
                storage_size, &info);

        AVS_UNIT_ASSERT_NOT_NULL(msg);

        avs_net_abstract_socket_t *backend =
                _anjay_coap_socket_get_backend(socket);
        AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_get_opt(
                backend, AVS_NET_SOCKET_OPT_MTU, &mtu));
        AVS_UNIT_ASSERT_EQUAL(mtu.mtu, 1500);
        AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_get_opt(
                backend, AVS_NET_SOCKET_OPT_INNER_MTU, &mtu));
        AVS_UNIT_ASSERT_EQUAL(mtu.mtu, 1472); // 20 bytes IPv4 + 8 bytes UDP

        AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_socket_send(socket, msg));

        anjay_coap_msg_t *recv_msg =
                (anjay_coap_msg_t *) alloca(COAP_MSG_MAX_SIZE);
        memset(recv_msg, 0, COAP_MSG_MAX_SIZE);
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_socket_recv(socket, recv_msg,
                                        COAP_MSG_MAX_SIZE));

        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(recv_msg, msg, msg->length);
        _anjay_coap_socket_cleanup(&socket);
        free(storage);
    }
    { // dtls_client_send_recv
        anjay_coap_socket_t *socket =
                _anjay_test_setup_dtls_echo_socket(TEST_PORT_DTLS);

        anjay_coap_msg_info_t info = _anjay_coap_msg_info_init();
        info.type = ANJAY_COAP_MSG_CONFIRMABLE;
        info.code = ANJAY_COAP_CODE_CONTENT;
        info.identity.msg_id = 4;

        size_t storage_size = COAP_MSG_MAX_SIZE;
        void *storage = malloc(storage_size);

        const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
                _anjay_coap_ensure_aligned_buffer(storage),
                storage_size, &info);

        AVS_UNIT_ASSERT_NOT_NULL(msg);

        avs_net_abstract_socket_t *backend =
                _anjay_coap_socket_get_backend(socket);
        AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_get_opt(
                backend, AVS_NET_SOCKET_OPT_MTU, &mtu));
        AVS_UNIT_ASSERT_EQUAL(mtu.mtu, 1500);
        AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_get_opt(
                backend, AVS_NET_SOCKET_OPT_INNER_MTU, &mtu));
        // The negotiated cipher is not well-defined, so it's a range:
        // -- minimum ---- maximum --------------------------------------------
        //         20           20      bytes of IPv4 header
        //          8            8      bytes of UDP header
        //         13           13      bytes of DTLS header
        //          0            8      bytes of explicit IV
        //          0           16      bytes of AEAD tag or MD+padding
        // --------------------------------------------------------------------
        //         41           65      bytes of headers subtracted from 1500
        AVS_UNIT_ASSERT_TRUE(mtu.mtu >= 1435 && mtu.mtu <= 1459);

        AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_socket_send(socket, msg));

        anjay_coap_msg_t *recv_msg =
                (anjay_coap_msg_t *) alloca(COAP_MSG_MAX_SIZE);
        memset(recv_msg, 0, COAP_MSG_MAX_SIZE);
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_socket_recv(socket, recv_msg,
                                        COAP_MSG_MAX_SIZE));

        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(recv_msg, msg, msg->length);
        _anjay_coap_socket_cleanup(&socket);
        free(storage);
    }
}
