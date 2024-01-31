/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_COAP_SRC_UDP_UDP_MSG_H
#define FLUF_COAP_SRC_UDP_UDP_MSG_H

#include <stddef.h>
#include <stdint.h>

#include "fluf_coap_udp_header.h"
#include "fluf_options.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {

    fluf_coap_udp_header_t header;

    fluf_coap_token_t token;

    fluf_coap_options_t *options;

    void *payload;

    size_t payload_size;

    size_t occupied_buff_size;
} fluf_coap_udp_msg_t;

int _fluf_coap_udp_msg_decode(fluf_coap_udp_msg_t *out_msg,
                              void *packet,
                              size_t packet_size);

int _fluf_coap_udp_header_serialize(fluf_coap_udp_msg_t *msg,
                                    uint8_t *buf,
                                    size_t buf_size);

int _fluf_coap_udp_msg_serialize(fluf_coap_udp_msg_t *msg,
                                 uint8_t *buf,
                                 size_t buf_size,
                                 size_t *out_bytes_written);

#ifdef __cplusplus
}
#endif

#endif // FLUF_COAP_SRC_UDP_UDP_MSG_H
