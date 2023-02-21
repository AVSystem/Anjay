/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_SRC_OBSERVE_H
#define AVS_COAP_SRC_OBSERVE_H

#include <avsystem/coap/observe.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    /** An ID (CoAP token) that uniquely identifies an observation. */
    avs_coap_observe_id_t id;

    /** Function to call when the observation is canceled. */
    avs_coap_observe_cancel_handler_t *cancel_handler;
    void *cancel_handler_arg;

    /** Last Observe option value sent to the server. */
    uint32_t last_observe_option_value;

    /**
     * Values present in the original Observe request.
     * Saved to match requests for notification blocks past the first one
     * in case of block-wise notifications.
     */
    uint8_t request_code;
    avs_coap_options_t request_key;

    /** Storage space for options. */
    uint8_t options_storage[];
} avs_coap_observe_t;

static inline uint32_t _avs_coap_observe_initial_option_value(void) {
    // Response to the original Observe request always set the option to 0.
    // Further notifications use larger values.
    return 0;
}

typedef struct {
    uint8_t request_code;
    avs_coap_options_t request_key;
    uint32_t observe_option_value;
} avs_coap_observe_notify_t;

avs_error_t
_avs_coap_observe_setup_notify(avs_coap_ctx_t *ctx,
                               const avs_coap_observe_id_t *id,
                               avs_coap_observe_notify_t *out_notify);

void _avs_coap_observe_cancel(avs_coap_ctx_t *ctx,
                              const avs_coap_observe_id_t *id);

VISIBILITY_PRIVATE_HEADER_END

#endif // AVS_COAP_SRC_OBSERVE_H
