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

#ifndef ANJAY_COAP_TEST_SERVERS_H
#define ANJAY_COAP_TEST_SERVERS_H

#include <stdint.h>

#include <avsystem/commons/unit/test.h>

#include "../socket.h"

anjay_coap_socket_t *_anjay_test_setup_dtls_echo_socket(uint16_t port);
anjay_coap_socket_t *_anjay_test_setup_udp_echo_socket(uint16_t port);

// sends the message back, setting msg type to ACK and code to 2.04 Content
anjay_coap_socket_t *_anjay_test_setup_udp_ack_echo_socket(uint16_t port);

// sends back a Reset message with the same token as the request
anjay_coap_socket_t *_anjay_test_setup_udp_reset_socket(uint16_t port);

// responds with 2 packets: ACK with mismatched ID and then a correct Reset
anjay_coap_socket_t *_anjay_test_setup_udp_mismatched_ack_then_reset_socket(uint16_t port);

// always responds with random data
anjay_coap_socket_t *_anjay_test_setup_udp_garbage_socket(uint16_t port);

// responds with 2 packets: Reset with mismatched ID and then a correct ACK
anjay_coap_socket_t *_anjay_test_setup_udp_mismatched_reset_then_ack_socket(uint16_t port);

// responds with 2 packets: random data and then a correct ACK
anjay_coap_socket_t *_anjay_test_setup_udp_garbage_then_ack_socket(uint16_t port);

// responds with 3 packets: bare ACK, mismatched NON with garbage and NON with echo
anjay_coap_socket_t *_anjay_test_setup_udp_long_separate_socket(uint16_t port);

#endif // ANJAY_COAP_TEST_SERVERS_H
