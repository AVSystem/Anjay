/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_COAP_MSG_DETAILS_H
#define ANJAY_COAP_MSG_DETAILS_H

#include "../anjay_utils_private.h"

#include <avsystem/coap/streaming.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct anjay_msg_details {
    uint8_t msg_code;
    uint16_t format;
    /* target URI path */
    AVS_LIST(const anjay_string_t) uri_path;
    AVS_LIST(const anjay_string_t) uri_query;
    /* path of the resource created using Create RPC */
    AVS_LIST(const anjay_string_t) location_path;
} anjay_msg_details_t;

static inline avs_error_t
_anjay_coap_fill_response_header(avs_coap_response_header_t *out_response,
                                 const anjay_msg_details_t *details) {
    *out_response = (avs_coap_response_header_t) {
        .code = details->msg_code
    };
    avs_error_t err = avs_coap_options_dynamic_init(&out_response->options);
    if (avs_is_err(err)) {
        return err;
    }
    (void) (avs_is_err((err = avs_coap_options_set_content_format(
                                &out_response->options, details->format)))
            || avs_is_err((err = _anjay_coap_add_string_options(
                                   &out_response->options,
                                   details->location_path,
                                   AVS_COAP_OPTION_LOCATION_PATH))));
    if (avs_is_err(err)) {
        avs_coap_options_cleanup(&out_response->options);
    }
    return err;
}

static inline avs_stream_t *
_anjay_coap_setup_response_stream(avs_coap_streaming_request_ctx_t *request_ctx,
                                  const anjay_msg_details_t *details) {
    avs_coap_response_header_t response;
    avs_stream_t *stream = NULL;

    if (avs_is_ok(_anjay_coap_fill_response_header(&response, details))) {
        stream = avs_coap_streaming_setup_response(request_ctx, &response);
    }

    avs_coap_options_cleanup(&response.options);
    return stream;
}

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_MSG_DETAILS_H
