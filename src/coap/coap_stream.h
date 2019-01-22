/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_COAP_STREAM_H
#define ANJAY_COAP_STREAM_H

#include <avsystem/commons/coap/ctx.h>
#include <avsystem/commons/coap/msg_builder.h>
#include <avsystem/commons/stream.h>

#include "../utils_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#define ANJAY_COAP_STREAM_EXTENSION 0x436F4150UL /* CoAP */

int _anjay_coap_stream_create(avs_stream_abstract_t **stream_,
                              avs_coap_ctx_t *coap_ctx,
                              uint8_t *in_buffer,
                              size_t in_buffer_size,
                              uint8_t *out_buffer,
                              size_t out_buffer_size);

typedef enum {
    ANJAY_COAP_OBSERVE_NONE,
    ANJAY_COAP_OBSERVE_REGISTER,
    ANJAY_COAP_OBSERVE_DEREGISTER
} anjay_coap_observe_t;

typedef struct anjay_msg_details {
    avs_coap_msg_type_t msg_type;
    uint8_t msg_code;
    uint16_t format;
    bool observe_serial;
    /* target URI path */
    AVS_LIST(const anjay_string_t) uri_path;
    AVS_LIST(const anjay_string_t) uri_query;
    /* path of the resource created using Create RPC */
    AVS_LIST(const anjay_string_t) location_path;
} anjay_msg_details_t;

typedef int
anjay_coap_stream_setup_response_t(avs_stream_abstract_t *stream,
                                   const anjay_msg_details_t *details);

typedef int anjay_coap_block_request_validator_t(const avs_coap_msg_t *msg,
                                                 void *arg);

typedef struct anjay_coap_stream_ext {
    anjay_coap_stream_setup_response_t *setup_response;
} anjay_coap_stream_ext_t;

int _anjay_coap_stream_get_tx_params(avs_stream_abstract_t *stream,
                                     avs_coap_tx_params_t *out_tx_params);

int _anjay_coap_stream_set_tx_params(avs_stream_abstract_t *stream,
                                     const avs_coap_tx_params_t *tx_params);

int _anjay_coap_stream_setup_response(avs_stream_abstract_t *stream,
                                      const anjay_msg_details_t *details);

int _anjay_coap_stream_setup_request(avs_stream_abstract_t *stream,
                                     const anjay_msg_details_t *details,
                                     const avs_coap_token_t *token);

int _anjay_coap_stream_set_error(avs_stream_abstract_t *stream, uint8_t code);

/** NOTE: Pointer acquired with this function is only valid until receiving next
 * CoAP packet. Note that this might mean invalidation during the same stream
 * exchange if block transfer is in progress. */
int _anjay_coap_stream_get_incoming_msg(avs_stream_abstract_t *stream,
                                        const avs_coap_msg_t **out_msg);

int _anjay_coap_stream_get_request_identity(
        avs_stream_abstract_t *stream, avs_coap_msg_identity_t *out_identity);

void _anjay_coap_stream_set_block_request_validator(
        avs_stream_abstract_t *stream,
        anjay_coap_block_request_validator_t *validator,
        void *validator_arg);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_STREAM_H
