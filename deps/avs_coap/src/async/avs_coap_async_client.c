/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

/*
 * Implementation of client-side asynchronous operations on
 * @ref avs_coap_exchange_t .
 */

#include <avs_coap_init.h>

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_utils.h>

#include <avsystem/coap/async_client.h>

#include "avs_coap_code_utils.h"
#include "avs_coap_exchange.h"

#include "async/avs_coap_async_client.h"

#define MODULE_NAME coap
#include <avs_coap_x_log_config.h>

#include "avs_coap_ctx.h"
#include "options/avs_coap_options.h"

VISIBILITY_SOURCE_BEGIN

typedef struct state_with_error {
    avs_coap_client_request_state_t state;
    avs_error_t error; // success iff state != FAIL
} state_with_error_t;

static inline state_with_error_t
success_state(avs_coap_client_request_state_t state) {
    assert(state != AVS_COAP_CLIENT_REQUEST_FAIL);
    return (state_with_error_t) {
        .state = state
    };
}

static inline state_with_error_t failure_state(avs_error_t error) {
    assert(avs_is_err(error));
    return (state_with_error_t) {
        .state = AVS_COAP_CLIENT_REQUEST_FAIL,
        .error = error
    };
}

static avs_error_t client_exchange_send_next_chunk(
        avs_coap_ctx_t *ctx, AVS_LIST(avs_coap_exchange_t) **exchange_ptr_ptr) {
    assert(exchange_ptr_ptr && *exchange_ptr_ptr && **exchange_ptr_ptr);
    AVS_ASSERT(AVS_LIST_FIND_PTR(&_avs_coap_get_base(ctx)->client_exchanges,
                                 **exchange_ptr_ptr)
                       != NULL,
               "not a started client exchange");

    avs_coap_exchange_id_t id = (**exchange_ptr_ptr)->id;

    // every request needs to have an unique token
    avs_coap_token_t old_token = (**exchange_ptr_ptr)->token;
    avs_error_t err =
            _avs_coap_ctx_generate_token(_avs_coap_get_base(ctx)->prng_ctx,
                                         &(**exchange_ptr_ptr)->token);
    if (avs_is_ok(err)) {
        err = _avs_coap_exchange_send_next_chunk(
                ctx, **exchange_ptr_ptr,
                (**exchange_ptr_ptr)->by_type.client.send_result_handler,
                (**exchange_ptr_ptr)->by_type.client.send_result_handler_arg);
        *exchange_ptr_ptr = _avs_coap_find_client_exchange_ptr_by_id(ctx, id);
        assert(!*exchange_ptr_ptr || **exchange_ptr_ptr);
    }
    if (*exchange_ptr_ptr && avs_is_err(err)) {
        (**exchange_ptr_ptr)->token = old_token;
    }
    return err;
}

#ifdef WITH_AVS_COAP_BLOCK
static inline size_t
initial_block2_option_size(avs_coap_ctx_t *ctx,
                           const avs_coap_exchange_t *exchange) {
    assert(exchange->by_type.client.next_response_payload_offset > 0);
    char buffer[64];
    avs_coap_options_t expected_options =
            avs_coap_options_create_empty(buffer, sizeof(buffer));
    avs_error_t err =
            avs_coap_options_add_block(&expected_options,
                                       &(avs_coap_option_block_t) {
                                           .type = AVS_COAP_BLOCK2,
                                           .seq_num = UINT16_MAX,
                                           .size = AVS_COAP_BLOCK_MAX_SIZE
                                       });
    assert(avs_is_ok(err));

    size_t block_size = avs_max_power_of_2_not_greater_than(
            avs_coap_max_incoming_message_payload(ctx, &expected_options,
                                                  AVS_COAP_CODE_CONTENT));
    if (block_size > AVS_COAP_BLOCK_MAX_SIZE) {
        block_size = AVS_COAP_BLOCK_MAX_SIZE;
    } else if (block_size < AVS_COAP_BLOCK_MIN_SIZE) {
        block_size = AVS_COAP_BLOCK_MIN_SIZE;
    }
    return block_size;
}
#endif // WITH_AVS_COAP_BLOCK

avs_error_t _avs_coap_client_exchange_send_first_chunk(
        avs_coap_ctx_t *ctx, AVS_LIST(avs_coap_exchange_t) **exchange_ptr_ptr) {
    assert(exchange_ptr_ptr && *exchange_ptr_ptr && **exchange_ptr_ptr);
    avs_error_t err = AVS_OK;
#ifdef WITH_AVS_COAP_BLOCK
    if ((**exchange_ptr_ptr)->by_type.client.next_response_payload_offset > 0
            && !_avs_coap_options_find_first_opt(&(**exchange_ptr_ptr)->options,
                                                 AVS_COAP_OPTION_BLOCK2)) {
        const size_t block_size =
                initial_block2_option_size(ctx, **exchange_ptr_ptr);
        if ((**exchange_ptr_ptr)->by_type.client.next_response_payload_offset
                >= block_size) {
            err = avs_coap_options_add_block(
                    &(**exchange_ptr_ptr)->options,
                    &(avs_coap_option_block_t) {
                        .type = AVS_COAP_BLOCK2,
                        .seq_num =
                                (uint32_t) ((**exchange_ptr_ptr)
                                                    ->by_type.client
                                                    .next_response_payload_offset
                                            / block_size),
                        .size = (uint16_t) block_size
                    });
        }
    }
#endif // WITH_AVS_COAP_BLOCK
    if (avs_is_ok(err)) {
        err = client_exchange_send_next_chunk(ctx, exchange_ptr_ptr);
    }
    return err;
}

static inline bool request_header_valid(const avs_coap_request_header_t *req) {
    if (!avs_coap_code_is_request(req->code)) {
        LOG(WARNING, _("non-request code ") "%s" _(" used in request header"),
            AVS_COAP_CODE_STRING(req->code));
        return false;
    }

    return _avs_coap_options_valid(&req->options);
}

static inline const char *
request_state_string(avs_coap_client_request_state_t result) {
    switch (result) {
    case AVS_COAP_CLIENT_REQUEST_OK:
        return "ok";
    case AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT:
        return "partial content";
    case AVS_COAP_CLIENT_REQUEST_FAIL:
        return "fail";
    case AVS_COAP_CLIENT_REQUEST_CANCEL:
        return "cancel";
    }

    return "<unknown>";
}

#ifdef WITH_AVS_COAP_BLOCK
static int get_response_block_option(const avs_coap_borrowed_msg_t *response,
                                     avs_coap_option_block_t *out_block2) {
    switch (avs_coap_options_get_block(&response->options, AVS_COAP_BLOCK2,
                                       out_block2)) {
    case 0:
        return 0;
    case AVS_COAP_OPTION_MISSING:
        return -1;
    default:
        AVS_UNREACHABLE("malformed option got through packet validation");
        return -1;
    }
}

static size_t
get_response_payload_offset(const avs_coap_borrowed_msg_t *response) {
    avs_coap_option_block_t block2;
    // response->payload_offset refers to payload offset in a single CoAP
    // message payload if it's received in chunks, which can happen if CoAP/TCP
    // is used.
    if (get_response_block_option(response, &block2)) {
        return response->payload_offset;
    }
    return block2.seq_num * block2.size + response->payload_offset;
}
#else  // WITH_AVS_COAP_BLOCK
static size_t
get_response_payload_offset(const avs_coap_borrowed_msg_t *response) {
    return response->payload_offset;
}
#endif // WITH_AVS_COAP_BLOCK

static void
call_exchange_response_handler(avs_coap_ctx_t *ctx,
                               avs_coap_exchange_t *exchange,
                               const avs_coap_borrowed_msg_t *response_msg,
                               size_t response_payload_offset,
                               state_with_error_t request_state) {
    assert(ctx);
    assert(exchange);

    LOG(TRACE, _("exchange ") "%s" _(": ") "%s",
        AVS_UINT64_AS_STRING(exchange->id.value),
        request_state_string(request_state.state));

    // TODO: T2243
    // Try to not create exchange if response handler isn't defined
    if (!exchange->by_type.client.handle_response) {
        return;
    }

    size_t expected_payload_offset =
            AVS_MIN(exchange->by_type.client.next_response_payload_offset,
                    response_payload_offset
                            + (response_msg ? response_msg->payload_size : 0));
    assert(expected_payload_offset >= response_payload_offset);
    assert(expected_payload_offset - response_payload_offset
           <= (response_msg ? response_msg->payload_size : 0));
    const avs_coap_client_async_response_t *exchange_response =
            !response_msg ? NULL
                          : &(const avs_coap_client_async_response_t) {
                                .header = (const avs_coap_response_header_t) {
                                    .code = response_msg->code,
                                    .options = response_msg->options
                                },
                                .payload_offset = expected_payload_offset,
                                .payload = (const char *) response_msg->payload
                                           + (expected_payload_offset
                                              - response_payload_offset),
                                .payload_size = response_msg->payload_size
                                                - (expected_payload_offset
                                                   - response_payload_offset)
                            };

    exchange->by_type.client.handle_response(
            ctx, exchange->id, request_state.state, exchange_response,
            request_state.error, exchange->by_type.client.handle_response_arg);
}

static void
call_partial_response_handler(avs_coap_ctx_t *ctx,
                              AVS_LIST(avs_coap_exchange_t) **exchange_ptr_ptr,
                              const avs_coap_borrowed_msg_t *response) {
    assert(response);
    assert(exchange_ptr_ptr && *exchange_ptr_ptr && **exchange_ptr_ptr);

    avs_coap_exchange_id_t exchange_id = (**exchange_ptr_ptr)->id;
    size_t response_payload_offset = get_response_payload_offset(response);

    // do not report PARTIAL_CONTENT unless there is some actual content
    // this avoids calling the handler for empty 2.31 Continue responses
    if (response->payload) {
        while (*exchange_ptr_ptr
               && response_payload_offset + response->payload_size
                          > (**exchange_ptr_ptr)
                                    ->by_type.client
                                    .next_response_payload_offset) {
            size_t expected_payload_offset =
                    (**exchange_ptr_ptr)
                            ->by_type.client.next_response_payload_offset;
            call_exchange_response_handler(
                    ctx, **exchange_ptr_ptr, response, response_payload_offset,
                    success_state(AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT));
            if ((*exchange_ptr_ptr = _avs_coap_find_client_exchange_ptr_by_id(
                         ctx, exchange_id))
                    && response
                    && (**exchange_ptr_ptr)
                                       ->by_type.client
                                       .next_response_payload_offset
                                   == expected_payload_offset) {
                (**exchange_ptr_ptr)
                        ->by_type.client.next_response_payload_offset =
                        response_payload_offset + response->payload_size;
            }
        }
    }
}

static void cleanup_exchange(avs_coap_ctx_t *ctx,
                             avs_coap_exchange_t *exchange,
                             const avs_coap_borrowed_msg_t *final_msg,
                             state_with_error_t request_state) {
    assert(ctx);
    assert(exchange);
    AVS_ASSERT(!AVS_LIST_FIND_PTR(&_avs_coap_get_base(ctx)->client_exchanges,
                                  exchange),
               "exchange must be detached");
    AVS_ASSERT(request_state.state != AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT,
               "cleanup_exchange must not be used for intermediate responses");

    size_t response_payload_offset =
            final_msg ? get_response_payload_offset(final_msg) : 0;
    call_exchange_response_handler(ctx, exchange, final_msg,
                                   response_payload_offset, request_state);
    AVS_LIST_DELETE(&exchange);
}

#ifdef WITH_AVS_COAP_BLOCK
static bool
exchange_expects_continue_response(const avs_coap_exchange_t *exchange) {
    avs_coap_option_block_t request_block1;
    return avs_coap_code_is_request(exchange->code)
           && avs_coap_options_get_block(&exchange->options, AVS_COAP_BLOCK1,
                                         &request_block1)
                      == 0
           && request_block1.has_more;
}

static avs_error_t handle_request_block_size_renegotiation(
        avs_coap_option_block_t *request_block,
        const avs_coap_option_block_t *response_block) {
    assert(request_block);
    assert(response_block);

    if (request_block->size == response_block->size) {
        return AVS_OK;
    } else if (request_block->size > response_block->size) {
        // TODO: should this be only allowed at the start of block-wise
        // transfer?
        assert(request_block->size % response_block->size == 0);

        uint32_t multiplier =
                (uint32_t) (request_block->size / response_block->size);
        uint32_t new_seq_num = request_block->seq_num * multiplier;

        if (new_seq_num > AVS_COAP_BLOCK_MAX_SEQ_NUMBER) {
            LOG(DEBUG,
                _("BLOCK") "%d" _(" size renegotiation impossible: seq_num "
                                  "overflows (") "%" PRIu32 _(" >= ") "%" PRIu32
                        _(" == 2^20), ignoring size renegotiation request"),
                request_block->type == AVS_COAP_BLOCK1 ? 1 : 2, new_seq_num,
                (uint32_t) AVS_COAP_BLOCK_MAX_SEQ_NUMBER);
        } else {
            LOG(DEBUG,
                _("BLOCK") "%d" _(" size renegotiated: ") "%" PRIu16
                        _(" -> ") "%" PRIu16 _("; seq_num ") "%" PRIu32 _(
                                " -> ") "%" PRIu32,
                request_block->type == AVS_COAP_BLOCK1 ? 1 : 2,
                request_block->size, response_block->size,
                request_block->seq_num, new_seq_num);

            request_block->seq_num = new_seq_num;
            request_block->size = response_block->size;
        }
        return AVS_OK;
    } else {
        assert(request_block->size < response_block->size);
        LOG(DEBUG,
            _("invalid BLOCK") "%d" _(" size increase requested (") "%" PRIu16
                    _(" -> ") "%" PRIu16 _("), ignoring"),
            request_block->type == AVS_COAP_BLOCK1 ? 1 : 2, request_block->size,
            response_block->size);
        return _avs_coap_err(AVS_COAP_ERR_BLOCK_SIZE_RENEGOTIATION_INVALID);
    }
}

static avs_error_t update_exchange_for_next_request_block(
        avs_coap_exchange_t *exchange,
        const avs_coap_option_block_t *response_block1) {
    assert(exchange_expects_continue_response(exchange));
    assert(!response_block1 || response_block1->type == AVS_COAP_BLOCK1);

    // Sending another block of a request requires keeping the same
    // set of CoAP options as the previous one, except for BLOCK1, whose
    // seq_num needs to be incremented.
    //
    // The CoAP server may also request the use of smaller blocks by sending
    // a response containing BLOCK1 option with the requested size.

    avs_coap_option_block_t request_block1;
    int opts_result =
            avs_coap_options_get_block(&exchange->options, AVS_COAP_BLOCK1,
                                       &request_block1);
    AVS_ASSERT(opts_result == 0, "BLOCK1 option invalid or missing in request");
    // request is controlled by us, it should be valid

    ++request_block1.seq_num;
    avs_error_t err;
    if (response_block1
            && avs_is_err((err = handle_request_block_size_renegotiation(
                                   &request_block1, response_block1)))) {
        return err;
    }

    if (request_block1.seq_num > AVS_COAP_BLOCK_MAX_SEQ_NUMBER) {
        LOG(ERROR,
            _("BLOCK1 sequence number (") "%" PRIu32 _(
                    ") exceeds maximum acceptable value (") "%" PRIu32 _(")"),
            request_block1.seq_num, (uint32_t) AVS_COAP_BLOCK_MAX_SEQ_NUMBER);
        return _avs_coap_err(AVS_COAP_ERR_BLOCK_SEQ_NUM_OVERFLOW);
    }

    avs_coap_options_remove_by_number(&exchange->options,
                                      AVS_COAP_OPTION_BLOCK1);
    avs_error_t opts_err =
            avs_coap_options_add_block(&exchange->options, &request_block1);
    AVS_ASSERT(avs_is_ok(opts_err),
               "options buffer is supposed to have enough space for options");

    return (!opts_result && avs_is_ok(opts_err))
                   ? AVS_OK
                   : _avs_coap_err(AVS_COAP_ERR_ASSERT_FAILED);
}

static state_with_error_t
handle_continue_response(avs_coap_ctx_t *ctx,
                         AVS_LIST(avs_coap_exchange_t) **exchange_ptr_ptr,
                         const avs_coap_borrowed_msg_t *response) {
    avs_coap_option_block_t response_block1;
    switch (avs_coap_options_get_block(&response->options, AVS_COAP_BLOCK1,
                                       &response_block1)) {
    case 0: {
        assert(exchange_ptr_ptr && *exchange_ptr_ptr && **exchange_ptr_ptr);
        // TODO: T2172 check that response_block1 matches request block1;
        // FAIL if it doesn't

        // TODO: should other response options be checked?

        avs_error_t err =
                update_exchange_for_next_request_block(**exchange_ptr_ptr,
                                                       &response_block1);
        if (avs_is_err(err)) {
            return failure_state(err);
        }

        call_partial_response_handler(ctx, exchange_ptr_ptr, response);

        // the call might have canceled the exchange
        // If we're finished with a single response packet, but not with the
        // whole exchange, then request more data from the server.
        assert(!*exchange_ptr_ptr || **exchange_ptr_ptr);
        if (*exchange_ptr_ptr
                && avs_is_err((err = client_exchange_send_next_chunk(
                                       ctx, exchange_ptr_ptr)))) {
            return failure_state(err);
        }

        return success_state(AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT);
    }

    case AVS_COAP_OPTION_MISSING:
        LOG(DEBUG, _("BLOCK1 option missing in ") "%s" _(" response"),
            AVS_COAP_CODE_STRING(response->code));
        return failure_state(_avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS));

    default:
        LOG(DEBUG, _("malformed BLOCK1 option in ") "%s" _(" response"),
            AVS_COAP_CODE_STRING(response->code));
        return failure_state(_avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS));
    }
}

static avs_error_t update_request_for_next_response_block(
        avs_coap_exchange_t *exchange,
        const avs_coap_option_block_t *response_block2) {
    assert(exchange);
    assert(response_block2);
    assert(response_block2->type == AVS_COAP_BLOCK2);

    // To request response blocks after the first one, we need to keep the same
    // set of CoAP options as in original one, except for:
    // * BLOCK1, which should be removed,
    // * BLOCK2, which should have seq_num incremented.
    //
    // Additionally, message token need to be changed

    // add/update BLOCK2 option in the request
    avs_coap_option_block_t block2;
    int opts_result = avs_coap_options_get_block(&exchange->options,
                                                 AVS_COAP_BLOCK2, &block2);
    AVS_ASSERT(opts_result >= 0,
               "exchange is supposed to have up to a single BLOCK2 option");
    const bool request_has_block2 = (opts_result != AVS_COAP_OPTION_MISSING);
    const uint32_t expected_offset =
            request_has_block2 ? block2.seq_num * block2.size : 0;
    const uint32_t actual_offset =
            response_block2->seq_num * response_block2->size;

    if (expected_offset != actual_offset) {
        LOG(DEBUG,
            _("mismatched response block offset (expected ") "%" PRIu32 _(
                    ", got ") "%" PRIu32 _(")"),
            expected_offset, actual_offset);
        return _avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS);
    }

    avs_error_t err;
    // If the request didn't have BLOCK2 option, any size is OK
    if (request_has_block2 && (block2.size != response_block2->size)) {
        err = handle_request_block_size_renegotiation(&block2, response_block2);
        if (avs_is_err(err)) {
            return err;
        }
    }

    // remove BLOCK1 option in the request (if any)
    avs_coap_options_remove_by_number(&exchange->options,
                                      AVS_COAP_OPTION_BLOCK1);
    avs_coap_options_remove_by_number(&exchange->options,
                                      AVS_COAP_OPTION_BLOCK2);
    block2 = (avs_coap_option_block_t) {
        .type = AVS_COAP_BLOCK2,
        .seq_num = (uint32_t) (exchange->by_type.client
                                       .next_response_payload_offset
                               / response_block2->size),
        .size = response_block2->size,
        .is_bert = response_block2->is_bert
    };
    AVS_ASSERT(block2.is_bert || block2.seq_num > response_block2->seq_num,
               "bug: invalid seq_num");
    if (avs_is_err((err = avs_coap_options_add_block(&exchange->options,
                                                     &block2)))) {
        AVS_ASSERT(err.category != AVS_COAP_ERR_CATEGORY
                           || err.code != AVS_COAP_ERR_MESSAGE_TOO_BIG,
                   "exchange is supposed to have enough space for adding extra "
                   "BLOCK option");
        return err;
    }

    // do not include payload any more
    exchange->write_payload = NULL;
    exchange->write_payload_arg = NULL;
    return AVS_OK;
}

static bool etag_matches(avs_coap_exchange_t *exchange,
                         const avs_coap_borrowed_msg_t *msg) {
    avs_coap_etag_t etag;
    int retval = avs_coap_options_get_etag(&msg->options, &etag);
    if (retval < 0) {
        return false;
    }
    if (!exchange->by_type.client.etag_stored) {
        exchange->by_type.client.etag = etag;
        // Empty ETag is stored if it isn't present in options.
        exchange->by_type.client.etag_stored = true;
        return true;
    }
    if (!avs_coap_etag_equal(&etag, &exchange->by_type.client.etag)) {
        LOG(WARNING,
            _("Response ETag mismatch: previous: ") "%s" _(", current: ") "%s",
            AVS_COAP_ETAG_HEX(&exchange->by_type.client.etag),
            AVS_COAP_ETAG_HEX(&etag));
        return false;
    }
    return true;
}

static state_with_error_t
handle_final_response(avs_coap_ctx_t *ctx,
                      AVS_LIST(avs_coap_exchange_t) **exchange_ptr_ptr,
                      const avs_coap_borrowed_msg_t *response) {
    assert(exchange_ptr_ptr && *exchange_ptr_ptr && **exchange_ptr_ptr);
    assert(response);

    // do not include any more payload in further requests
    (**exchange_ptr_ptr)->write_payload = NULL;
    (**exchange_ptr_ptr)->write_payload_arg = NULL;
    (**exchange_ptr_ptr)->eof_cache.empty = true;

    if (!etag_matches(**exchange_ptr_ptr, response)) {
        return failure_state(_avs_coap_err(AVS_COAP_ERR_ETAG_MISMATCH));
    }

    avs_coap_option_block_t request_block2;
    int opts_result =
            avs_coap_options_get_block(&(**exchange_ptr_ptr)->options,
                                       AVS_COAP_BLOCK2, &request_block2);
    AVS_ASSERT(opts_result == 0 || opts_result == AVS_COAP_OPTION_MISSING,
               "library allowed for construction of a malformed request");
    bool request_has_block2 = (opts_result != AVS_COAP_OPTION_MISSING);

    avs_coap_option_block_t response_block2;
    switch (avs_coap_options_get_block(&response->options, AVS_COAP_BLOCK2,
                                       &response_block2)) {
    case 0: {
        // BLOCK response to a request, which may or may not have had an
        // explicit BLOCK2 option

        size_t request_off = request_has_block2 ? request_block2.seq_num
                                                          * request_block2.size
                                                : 0;
        size_t response_off = response_block2.seq_num * response_block2.size;
        if (request_off != response_off) {
            // We asked the server for one block of data, but it returned
            // another one. This is clearly a server-side error.
            LOG(WARNING, _("expected ") "%s" _(", got ") "%s",
                _AVS_COAP_OPTION_BLOCK_STRING(&request_block2),
                _AVS_COAP_OPTION_BLOCK_STRING(&response_block2));
            return failure_state(_avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS));
        }

        // TODO T2123: check that all options other than BLOCK2 are identical
        // across responses

        LOG(TRACE, _("exchange ") "%s" _(": ") "%s",
            AVS_UINT64_AS_STRING((**exchange_ptr_ptr)->id.value),
            _AVS_COAP_OPTION_BLOCK_STRING(&response_block2));

        if (response_block2.has_more) {
            call_partial_response_handler(ctx, exchange_ptr_ptr, response);

            // the call might have canceled the exchange
            assert(!*exchange_ptr_ptr || **exchange_ptr_ptr);
            if (*exchange_ptr_ptr) {
                // We're finished with a single response packet, but not with
                // the whole exchange. Request more data from the server.
                avs_error_t err = update_request_for_next_response_block(
                        **exchange_ptr_ptr, &response_block2);
                if (err.category == AVS_ERRNO_CATEGORY
                        && err.code == AVS_ERANGE) {
                    // Requested offset larger than allowed by CoAP spec -
                    // treat this as the end of the transfer
                    return success_state(AVS_COAP_CLIENT_REQUEST_OK);
                }
                if (avs_is_err(err)
                        || avs_is_err((err = client_exchange_send_next_chunk(
                                               ctx, exchange_ptr_ptr)))) {
                    return failure_state(err);
                }
            }

            return success_state(AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT);
        } else {
            // final block of a BLOCK2 response
            return success_state(AVS_COAP_CLIENT_REQUEST_OK);
        }
    }

    case AVS_COAP_OPTION_MISSING:
        // We asked the server for a block of data, but server responded
        // with a non-BLOCK response. This most likely indicates a server
        // error.
        if (request_has_block2) {
            LOG(DEBUG, _("expected ") "%s" _(", but BLOCK2 option not found"),
                _AVS_COAP_OPTION_BLOCK_STRING(&request_block2));
            return failure_state(_avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS));
        }

        // Non-BLOCK response to a non-BLOCK request
        return success_state(AVS_COAP_CLIENT_REQUEST_OK);

    default:
        LOG(DEBUG, _("malformed BLOCK2 option"));
        return failure_state(_avs_coap_err(AVS_COAP_ERR_MALFORMED_OPTIONS));
    }
}
#else  // WITH_AVS_COAP_BLOCK
static avs_error_t update_exchange_for_next_request_block(
        avs_coap_exchange_t *exchange,
        const avs_coap_option_block_t *response_block1) {
    (void) exchange;
    (void) response_block1;

    AVS_UNREACHABLE(
            "More data to send even though BLOCK is disabled - this should be "
            "handled in _avs_coap_exchange_send_next_chunk()");
    return _avs_coap_err(AVS_COAP_ERR_ASSERT_FAILED);
}

static state_with_error_t
handle_final_response(avs_coap_ctx_t *ctx,
                      AVS_LIST(avs_coap_exchange_t) **exchange_ptr_ptr,
                      const avs_coap_borrowed_msg_t *response) {
    (void) ctx;
    assert(exchange_ptr_ptr && *exchange_ptr_ptr && **exchange_ptr_ptr);
    assert(response);

    (**exchange_ptr_ptr)->write_payload = NULL;
    (**exchange_ptr_ptr)->write_payload_arg = NULL;
    (**exchange_ptr_ptr)->eof_cache.empty = true;

    return success_state(AVS_COAP_CLIENT_REQUEST_OK);
}
#endif // WITH_AVS_COAP_BLOCK

static avs_error_t
client_exchange_send_all(avs_coap_ctx_t *ctx,
                         AVS_LIST(avs_coap_exchange_t) **exchange_ptr_ptr) {
    assert(exchange_ptr_ptr && *exchange_ptr_ptr && **exchange_ptr_ptr);
    assert((**exchange_ptr_ptr)->by_type.client.handle_response == NULL);

    avs_error_t err = client_exchange_send_next_chunk(ctx, exchange_ptr_ptr);
    while (avs_is_ok(err) && *exchange_ptr_ptr
           && !(**exchange_ptr_ptr)->eof_cache.empty) {
        (void) (avs_is_err((err = update_exchange_for_next_request_block(
                                    **exchange_ptr_ptr, NULL)))
                || avs_is_err((err = client_exchange_send_next_chunk(
                                       ctx, exchange_ptr_ptr))));
    }

    if (*exchange_ptr_ptr) {
        if (avs_is_err(err)) {
            cleanup_exchange(ctx, AVS_LIST_DETACH(*exchange_ptr_ptr), NULL,
                             failure_state(err));
        } else {
            avs_coap_exchange_cancel(ctx, (**exchange_ptr_ptr)->id);
        }
        *exchange_ptr_ptr = NULL;
    }
    return err;
}

bool _avs_coap_client_exchange_request_sent(
        const avs_coap_exchange_t *exchange) {
    // Token is initialized in _avs_coap_client_exchange_send_next_chunk() and
    // zero-length tokens are never used. Hence, zero-length token means that
    // no request packets have been sent yet.
    return exchange->token.size > 0;
}

static avs_error_t
client_exchange_start(avs_coap_ctx_t *ctx,
                      AVS_LIST(avs_coap_exchange_t) *exchange_ptr,
                      avs_coap_exchange_id_t *out_id) {
    assert(exchange_ptr);
    assert(*exchange_ptr);

    AVS_LIST(avs_coap_exchange_t) *insert_ptr =
            &_avs_coap_get_base(ctx)->client_exchanges;
    // client_exchanges list containts exchanges for which the first request
    // packet has not been sent yet at the beginning. Add the new exchange after
    // all such existing exchanges, but before any others.
    while (*insert_ptr
           && !_avs_coap_client_exchange_request_sent(*insert_ptr)) {
        AVS_LIST_ADVANCE_PTR(&insert_ptr);
    }
    AVS_LIST_INSERT(insert_ptr, *exchange_ptr);
    assert(*insert_ptr == *exchange_ptr);
    (*exchange_ptr)->id = _avs_coap_generate_exchange_id(ctx);

    avs_error_t err = AVS_OK;
    if ((*exchange_ptr)->by_type.client.handle_response) {
        *out_id = (*exchange_ptr)->id;
        _avs_coap_reschedule_retry_or_request_expired_job(
                ctx, avs_time_monotonic_now());
    } else {
        *out_id = AVS_COAP_EXCHANGE_ID_INVALID;
        err = client_exchange_send_all(ctx, &insert_ptr);
    }

    *exchange_ptr = NULL;
    return err;
}

static state_with_error_t
handle_response(avs_coap_ctx_t *ctx,
                AVS_LIST(avs_coap_exchange_t) **exchange_ptr_ptr,
                const avs_coap_borrowed_msg_t *response) {
    assert(response);
    assert(exchange_ptr_ptr && *exchange_ptr_ptr && **exchange_ptr_ptr);

    switch (response->code) {
    case AVS_COAP_CODE_CONTINUE:
#ifdef WITH_AVS_COAP_BLOCK
        if (!exchange_expects_continue_response(**exchange_ptr_ptr)) {
            LOG(DEBUG, _("unexpected ") "%s" _(" response"),
                AVS_COAP_CODE_STRING(response->code));
            return failure_state(
                    _avs_coap_err(AVS_COAP_ERR_UNEXPECTED_CONTINUE_RESPONSE));
        }

        return handle_continue_response(ctx, exchange_ptr_ptr, response);
#else  // WITH_AVS_COAP_BLOCK
        LOG(DEBUG, _("unexpected ") "%s" _(" response"),
            AVS_COAP_CODE_STRING(response->code));
        return failure_state(_avs_coap_err(AVS_COAP_ERR_FEATURE_DISABLED));
#endif // WITH_AVS_COAP_BLOCK

    case AVS_COAP_CODE_REQUEST_ENTITY_TOO_LARGE:
        // TODO: T2171 handle Request Entity Too Large
        return failure_state(_avs_coap_err(AVS_COAP_ERR_NOT_IMPLEMENTED));

    default:
        return handle_final_response(ctx, exchange_ptr_ptr, response);
    }
}

#ifdef WITH_AVS_COAP_BLOCK
static state_with_error_t
handle_failure(avs_coap_ctx_t *ctx,
               AVS_LIST(avs_coap_exchange_t) **exchange_ptr_ptr,
               const avs_coap_borrowed_msg_t *response,
               avs_error_t fail_err) {
    assert(exchange_ptr_ptr && *exchange_ptr_ptr && **exchange_ptr_ptr);
    if (fail_err.category != AVS_COAP_ERR_CATEGORY
            || fail_err.code != AVS_COAP_ERR_TRUNCATED_MESSAGE_RECEIVED
            || !response) {
        return failure_state(fail_err);
    }
    // We received response, but it was too big to be held in our internal
    // buffer. Since we know our internal buffer size we may try resending
    // our request but with BLOCK2 option adjusted accordingly.
    avs_coap_option_block_t block2;
    int result = avs_coap_options_get_block(&response->options, AVS_COAP_BLOCK2,
                                            &block2);
    assert(result == 0 || result == AVS_COAP_OPTION_MISSING);

    size_t max_payload_size = ctx->vtable->max_incoming_payload_size(
            ctx, response->token.size, &response->options, response->code);
    if (result == AVS_COAP_OPTION_MISSING) {
        // There were no BLOCK2 in response, but we intend to use it, which'd
        // force the peer to repeat it, thus increasing the message overhead.
        if (max_payload_size >= AVS_COAP_OPT_BLOCK_MAX_SIZE) {
            max_payload_size -= AVS_COAP_OPT_BLOCK_MAX_SIZE;
        }
        block2 = (avs_coap_option_block_t) {
            .type = AVS_COAP_BLOCK2
        };
    }
    size_t new_max_block_size =
            avs_max_power_of_2_not_greater_than(max_payload_size);

    if (new_max_block_size > AVS_COAP_BLOCK_MAX_SIZE) {
        new_max_block_size = AVS_COAP_BLOCK_MAX_SIZE;
    }
    if (new_max_block_size < AVS_COAP_BLOCK_MIN_SIZE) {
        return failure_state(fail_err);
    }
    assert(new_max_block_size != block2.size);

    assert(block2.type == AVS_COAP_BLOCK2);
    block2.size = (uint16_t) new_max_block_size;
    block2.seq_num =
            (uint32_t) ((**exchange_ptr_ptr)
                                ->by_type.client.next_response_payload_offset
                        / new_max_block_size);

    // Replace or add BLOCK2 option to our request, so that the response would
    // likely fit into the input buffer.
    avs_coap_options_remove_by_number(&(**exchange_ptr_ptr)->options,
                                      AVS_COAP_OPTION_BLOCK2);

    avs_error_t send_err;
    if (avs_is_err((send_err = avs_coap_options_add_block(
                            &(**exchange_ptr_ptr)->options, &block2)))
            || avs_is_err((send_err = client_exchange_send_next_chunk(
                                   ctx, exchange_ptr_ptr)))) {
        return failure_state(avs_is_ok(fail_err) ? send_err : fail_err);
    }
    return success_state(AVS_COAP_CLIENT_REQUEST_OK);
}
#else  // WITH_AVS_COAP_BLOCK
static state_with_error_t
handle_failure(avs_coap_ctx_t *ctx,
               AVS_LIST(avs_coap_exchange_t) **exchange_ptr_ptr,
               const avs_coap_borrowed_msg_t *response,
               avs_error_t fail_err) {
    (void) ctx;
    (void) exchange_ptr_ptr;
    (void) response;
    return failure_state(fail_err);
}
#endif // WITH_AVS_COAP_BLOCK

static avs_coap_send_result_handler_result_t
on_request_delivery_finished(avs_coap_ctx_t *ctx,
                             avs_coap_send_result_t result,
                             avs_error_t fail_err,
                             const avs_coap_borrowed_msg_t *response,
                             void *exchange) {
    assert(!response || avs_coap_code_is_response(response->code));

    avs_coap_exchange_id_t exchange_id = ((avs_coap_exchange_t *) exchange)->id;
    AVS_LIST(avs_coap_exchange_t) *exchange_ptr =
            _avs_coap_find_client_exchange_ptr_by_id(ctx, exchange_id);
    if (!exchange_ptr) {
        assert(result == AVS_COAP_SEND_RESULT_CANCEL);
        return AVS_COAP_RESPONSE_ACCEPTED;
    }
    assert(*exchange_ptr == exchange);

    state_with_error_t request_state;
    switch (result) {
    case AVS_COAP_SEND_RESULT_PARTIAL_CONTENT:
        call_partial_response_handler(ctx, &exchange_ptr, response);
        return AVS_COAP_RESPONSE_ACCEPTED;

    case AVS_COAP_SEND_RESULT_OK:
        request_state = handle_response(ctx, &exchange_ptr, response);
        if (request_state.state == AVS_COAP_CLIENT_REQUEST_PARTIAL_CONTENT) {
            return AVS_COAP_RESPONSE_ACCEPTED;
        }
        break;

    case AVS_COAP_SEND_RESULT_FAIL:
        request_state = handle_failure(ctx, &exchange_ptr, response, fail_err);
        if (request_state.state == AVS_COAP_CLIENT_REQUEST_OK) {
            // we recovered from failure
            return AVS_COAP_RESPONSE_ACCEPTED;
        }
        break;

    case AVS_COAP_SEND_RESULT_CANCEL:
        request_state = success_state(AVS_COAP_CLIENT_REQUEST_CANCEL);
        break;
    }

    if (request_state.state == AVS_COAP_CLIENT_REQUEST_FAIL) {
        // We may end up here if a response was received, but during handling
        // at this layer we realize it is not well-formed, or that we cannot
        // continue a BLOCK-wise transfer.
        response = NULL;
    }

    assert(!exchange_ptr || *exchange_ptr);
    if (exchange_ptr) {
        cleanup_exchange(ctx, AVS_LIST_DETACH(exchange_ptr), response,
                         request_state);
    }

    return AVS_COAP_RESPONSE_ACCEPTED;
}

static AVS_LIST(avs_coap_exchange_t) client_exchange_create(
        uint8_t code,
        const avs_coap_options_t *options,
        avs_coap_payload_writer_t *payload_writer,
        void *payload_writer_arg,
        avs_coap_client_async_response_handler_t *response_handler,
        void *response_handler_arg) {
    assert(avs_coap_code_is_request(code));

    // Add a few extra bytes for BLOCK1 option in case the request turns out
    // to be large
    size_t options_capacity = options->capacity + AVS_COAP_OPT_BLOCK_MAX_SIZE;

    AVS_LIST(avs_coap_exchange_t) exchange =
            (AVS_LIST(avs_coap_exchange_t)) AVS_LIST_NEW_BUFFER(
                    sizeof(avs_coap_exchange_t) + options_capacity);
    if (!exchange) {
        return NULL;
    }

    size_t next_response_payload_offset = 0;
#ifdef WITH_AVS_COAP_BLOCK
    avs_coap_option_block_t block2;
    if (avs_coap_options_get_block(options, AVS_COAP_BLOCK2, &block2) == 0) {
        next_response_payload_offset = block2.seq_num * block2.size;
    }
#endif // WITH_AVS_COAP_BLOCK

    *exchange = (avs_coap_exchange_t) {
        .id = AVS_COAP_EXCHANGE_ID_INVALID,
        .write_payload = payload_writer,
        .write_payload_arg = payload_writer_arg,
        .code = code,
        .eof_cache = {
            .empty = true
        },
        .by_type = {
            .client = {
                .handle_response = response_handler,
                .handle_response_arg = response_handler_arg,
                .send_result_handler =
                        response_handler ? on_request_delivery_finished : NULL,
                .send_result_handler_arg = exchange,
                .next_response_payload_offset = next_response_payload_offset
            }
        },
        .options_buffer_size = options_capacity
    };

    // T2393 [COMPOUND-LITERAL-FAM-ASSIGNMENT-TRAP]
    //
    // Putting this initializer within the compound literal above makes some
    // compilers overwrite initial bytes of exchange->options_buffer with
    // nullbytes *after* options are copied, resulting in overwriting options
    // data.
    //
    // This is caused by combination of a few facts:
    // - FAM may be inserted in place of existing padding at the end of the
    //   struct,
    // - value of padding bytes is indeterminate,
    // - assignment of a compound literal is split into two parts:
    //   initialization of the compound literal itself, and copying it to actual
    //   destination.
    //
    // In particular, this means that the assignment above may include:
    //
    // 1. initializing the compound literal:
    //
    //    a. invoking any side-effects of expressions used for initialization
    //       of struct members,
    //    b. optionally filling padding bytes in the struct with zeros
    //
    // 2. copying the compound literal, including all padding bytes, to the
    //    target destination.
    //
    // Now, exchange->options_buffer is filled with usable data in step 1a.
    // If the exchange->options_buffer happens to be a FAM that overlaps with
    // padding bytes at the end of *exchange, and the compiler decides to do
    // step 1b, usable data will get overwritten with zeros in step 2.
    //
    // This behavior was observed on 32-bit armv7 (GCC 8.2 and Clang 7.0).
    // x86_64 miraculously works, even when compiling with -m32.
    exchange->options =
            _avs_coap_options_copy(options, exchange->options_buffer,
                                   options_capacity);

    return exchange;
}

avs_error_t avs_coap_client_send_async_request(
        avs_coap_ctx_t *ctx,
        avs_coap_exchange_id_t *out_exchange_id,
        const avs_coap_request_header_t *req,
        avs_coap_payload_writer_t *request_writer,
        void *request_writer_arg,
        avs_coap_client_async_response_handler_t *response_handler,
        void *response_handler_arg) {
    assert(ctx);
    assert(ctx->vtable);
    assert(req);

    if (!request_header_valid(req)) {
        return avs_errno(AVS_EINVAL);
    }

    AVS_LIST(avs_coap_exchange_t) exchange =
            client_exchange_create(req->code, &req->options, request_writer,
                                   request_writer_arg, response_handler,
                                   response_handler_arg);
    if (!exchange) {
        return avs_errno(AVS_ENOMEM);
    }

    avs_coap_exchange_id_t exchange_id;
    avs_error_t err = client_exchange_start(ctx, &exchange, &exchange_id);
    assert(!exchange);

    if (avs_is_err(err)) {
        if (avs_coap_exchange_id_valid(exchange_id)) {
            AVS_LIST(avs_coap_exchange_t) *exchange_ptr =
                    _avs_coap_find_client_exchange_ptr_by_id(ctx, exchange_id);
            if (exchange_ptr) {
                // Not using _avs_coap_client_exchange_cleanup() or
                // cleanup_exchange(), because this function's docs say that
                // response_handler is not called on error.
                AVS_LIST_DELETE(exchange_ptr);
            }
        }
        return err;
    }

    if (out_exchange_id) {
        *out_exchange_id = exchange_id;
    }
    return AVS_OK;
}

void _avs_coap_client_exchange_cleanup(avs_coap_ctx_t *ctx,
                                       avs_coap_exchange_t *exchange,
                                       avs_error_t err) {
    AVS_ASSERT(!AVS_LIST_FIND_PTR(&_avs_coap_get_base(ctx)->client_exchanges,
                                  exchange),
               "exchange must be detached");
    assert(avs_coap_code_is_request(exchange->code));

    if (_avs_coap_client_exchange_request_sent(exchange)) {
        ctx->vtable->abort_delivery(ctx, AVS_COAP_EXCHANGE_CLIENT_REQUEST,
                                    &exchange->token,
                                    avs_is_err(err)
                                            ? AVS_COAP_SEND_RESULT_FAIL
                                            : AVS_COAP_SEND_RESULT_CANCEL,
                                    err);
    }
    cleanup_exchange(ctx, exchange, NULL,
                     avs_is_err(err)
                             ? failure_state(err)
                             : success_state(AVS_COAP_CLIENT_REQUEST_CANCEL));
}

avs_error_t avs_coap_client_set_next_response_payload_offset(
        avs_coap_ctx_t *ctx,
        avs_coap_exchange_id_t exchange_id,
        size_t next_response_payload_offset) {
    avs_coap_exchange_t *exchange = NULL;
    if (avs_coap_exchange_id_valid(exchange_id)) {
        exchange = _avs_coap_find_client_exchange_by_id(ctx, exchange_id);
    }
    if (!exchange) {
        return avs_errno(AVS_ENOENT);
    }
    // NOTE: The second clause creates a special exception that allows
    // explicitly setting offset to 0 if the first request has not been sent yet
    if (next_response_payload_offset
                    <= exchange->by_type.client.next_response_payload_offset
            && (next_response_payload_offset > 0 || exchange->token.size > 0)) {
        return avs_errno(AVS_EINVAL);
    }
    exchange->by_type.client.next_response_payload_offset =
            next_response_payload_offset;
    return AVS_OK;
}
