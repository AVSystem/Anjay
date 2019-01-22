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

#include <anjay_config.h>

#define ANJAY_COAP_STREAM_INTERNALS

#include "server_internal.h"

#include <anjay_modules/time_defs.h>

#include <inttypes.h>

#include "../coap_log.h"

#include <avsystem/commons/coap/block_utils.h>
#include <avsystem/commons/memory.h>

#include "../content_format.h"
#include "../id_source/static.h"
#include "common.h"

VISIBILITY_SOURCE_BEGIN

#ifdef WITH_BLOCK_SEND
#    define has_block_ctx(server) ((server)->block_ctx)
#else
#    define has_block_ctx(server) (false)
#endif

static inline bool has_error(coap_server_t *server) {
    return server->last_error_code != 0;
}

static inline void clear_error(coap_server_t *server) {
    server->last_error_code = 0;
}

static bool is_server_reset(const coap_server_t *server) {
    return server->state == COAP_SERVER_STATE_RESET;
}

void _anjay_coap_server_reset(coap_server_t *server) {
    server->state = COAP_SERVER_STATE_RESET;
    AVS_LIST_CLEAR(&server->expected_block_opts);
    server->curr_block.valid = false;
    clear_error(server);
#ifdef WITH_BLOCK_SEND
    memset(&server->block_relation_validator, 0,
           sizeof(server->block_relation_validator));
#endif // WITH_BLOCK_SEND
}

#ifdef WITH_BLOCK_SEND
void _anjay_coap_server_set_block_request_relation_validator(
        coap_server_t *server,
        anjay_coap_block_request_validator_t *validator,
        void *validator_arg) {
    server->block_relation_validator.validator = validator;
    server->block_relation_validator.validator_arg = validator_arg;
}
#endif // WITH_BLOCK_SEND

const avs_coap_msg_identity_t *
_anjay_coap_server_get_request_identity(const coap_server_t *server) {
    if (server->state != COAP_SERVER_STATE_RESET) {
        return &server->request_identity;
    } else {
        return NULL;
    }
}

static bool is_block1_transfer(coap_server_t *server) {
    return server->state == COAP_SERVER_STATE_HAS_BLOCK1_REQUEST
           || server->state == COAP_SERVER_STATE_NEEDS_NEXT_BLOCK;
}

static bool is_success_response(uint8_t msg_code) {
    return avs_coap_msg_code_get_class(msg_code) == 2;
}

int _anjay_coap_server_setup_response(coap_server_t *server,
                                      const anjay_msg_details_t *details) {
    if (is_server_reset(server)) {
        coap_log(DEBUG, "no request to respond to");
        return -1;
    }

    AVS_ASSERT(!has_error(server), "setup_response called with unsent error");
    clear_error(server);

    if (!_anjay_coap_out_is_reset(&server->common.out)) {
        coap_log(TRACE, "setup_response called, but out buffer not reset");
        _anjay_coap_out_reset(&server->common.out);
    }

    const avs_coap_block_info_t *block = NULL;
    if (is_block1_transfer(server) && is_success_response(details->msg_code)) {
        block = &server->curr_block;
    }

    _anjay_coap_out_setup_mtu(&server->common.out, server->common.socket);
    return _anjay_coap_out_setup_msg(&server->common.out,
                                     &server->request_identity, details, block);
}

void _anjay_coap_server_set_error(coap_server_t *server, uint8_t code) {
    if (has_error(server)) {
        coap_log(DEBUG, "error %s skipped (%s still not sent)",
                 AVS_COAP_CODE_STRING(code),
                 AVS_COAP_CODE_STRING(server->last_error_code));
    }

    server->last_error_code = code;
    coap_log(DEBUG, "server error set to %s", AVS_COAP_CODE_STRING(code));
}

static void setup_error_response(coap_server_t *server) {
    assert(has_error(server));

    const anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = server->last_error_code,
        .format = AVS_COAP_FORMAT_NONE
    };

    _anjay_coap_out_reset(&server->common.out);
    clear_error(server);
    int result = _anjay_coap_server_setup_response(server, &details);
    assert(result == 0);

    (void) result;
}

int _anjay_coap_server_finish_response(coap_server_t *server) {
    if (has_error(server)) {
        setup_error_response(server);
    }

    if (has_block_ctx(server)) {
        int result = _anjay_coap_block_transfer_finish(server->block_ctx);
        server->request_identity =
                _anjay_coap_block_response_last_request_id(server->block_ctx);
        _anjay_coap_block_transfer_delete(&server->block_ctx);
        _anjay_coap_id_source_release(&server->static_id_source);
        return result;
    }

    int result = 0;
    if (is_block1_transfer(server)) {
        result = _anjay_coap_out_update_msg_header(&server->common.out,
                                                   &server->request_identity,
                                                   &server->curr_block);
    }

    if (!result) {
        const avs_coap_msg_t *msg =
                _anjay_coap_out_build_msg(&server->common.out);
        result = avs_coap_ctx_send(server->common.coap_ctx,
                                   server->common.socket, msg);
    }
    return result;
}

static bool is_opt_critical(uint32_t opt_number) {
    return opt_number % 2;
}

static int block_store_critical_options(AVS_LIST(coap_block_optbuf_t) *out,
                                        const avs_coap_msg_t *msg,
                                        uint32_t optnum_to_ignore) {
    AVS_LIST(coap_block_optbuf_t) *outptr = out;
    assert(!*outptr);

    for (avs_coap_opt_iterator_t optit = avs_coap_opt_begin(msg);
         !avs_coap_opt_end(&optit);
         avs_coap_opt_next(&optit)) {
        uint32_t optnum = avs_coap_opt_number(&optit);
        if (optnum == optnum_to_ignore || !is_opt_critical(optnum)) {
            continue;
        }
        uint32_t length = avs_coap_opt_content_length(optit.curr_opt);
        *outptr = (AVS_LIST(coap_block_optbuf_t)) AVS_LIST_NEW_BUFFER(
                offsetof(coap_block_optbuf_t, content) + length);
        if (!*outptr) {
            goto err;
        }
        (*outptr)->optnum = optnum;
        (*outptr)->length = length;
        memcpy((*outptr)->content, avs_coap_opt_value(optit.curr_opt), length);
        AVS_LIST_ADVANCE_PTR(&outptr);
    }
    return 0;
err:
    AVS_LIST_CLEAR(out);
    return -1;
}

static inline uint32_t get_block_offset(const avs_coap_block_info_t *block) {
    assert(avs_coap_is_valid_block_size(block->size));

    return block->seq_num * block->size;
}

typedef enum process_result {
    /** The message is a correct request, a basic one or the first BLOCK */
    PROCESS_INITIAL_OK,

    /** Not a valid request message. last_error_code may be set to enforce a
     * particular response code. */
    PROCESS_INITIAL_INVALID_REQUEST,
} process_result_t;

static process_result_t process_initial_request(coap_server_t *server,
                                                const avs_coap_msg_t *msg) {
    assert(is_server_reset(server));

    avs_coap_msg_type_t type = avs_coap_msg_get_type(msg);
    if (!avs_coap_msg_is_request(msg)
            // incoming Reset may still require some kind of reaction,
            // so it should be handled by upper layers
            && type != AVS_COAP_MSG_RESET) {
        coap_log(DEBUG, "invalid request: %s",
                 AVS_COAP_CODE_STRING(avs_coap_msg_get_code(msg)));
        return PROCESS_INITIAL_INVALID_REQUEST;
    }

    avs_coap_block_info_t block1;
    avs_coap_block_info_t block2;
    int result1 = avs_coap_get_block_info(msg, AVS_COAP_BLOCK1, &block1);
    int result2 = avs_coap_get_block_info(msg, AVS_COAP_BLOCK2, &block2);
    if (result1 || result2) {
        _anjay_coap_server_set_error(server, -ANJAY_ERR_BAD_REQUEST);
        return PROCESS_INITIAL_INVALID_REQUEST;
    }
    /**
     * CoAP supports bidirectional block communication, but LwM2M does not have
     * any operation for which it would be useful. Therefore it's not
     * implemented.
     */
    if (block1.valid && block2.valid) {
        _anjay_coap_server_set_error(server, -ANJAY_ERR_BAD_OPTION);
        return PROCESS_INITIAL_INVALID_REQUEST;
    }
    server->state = COAP_SERVER_STATE_HAS_REQUEST;

    if (block1.valid) {
        server->curr_block = block1;
        server->state = COAP_SERVER_STATE_HAS_BLOCK1_REQUEST;
    } else if (block2.valid) {
        server->curr_block = block2;
        server->state = COAP_SERVER_STATE_HAS_BLOCK2_REQUEST;
    }

    if (block1.valid || block2.valid) {
        coap_log(TRACE, "block request: %" PRIu32 ", size %u",
                 get_block_offset(&server->curr_block),
                 server->curr_block.size);

        if (server->curr_block.seq_num != 0) {
            coap_log(ERROR, "initial block seq_num nonzero");
            _anjay_coap_server_set_error(server,
                                         -ANJAY_ERR_REQUEST_ENTITY_INCOMPLETE);
            return PROCESS_INITIAL_INVALID_REQUEST;
        }

        if (block1.valid
                && block_store_critical_options(&server->expected_block_opts,
                                                msg, AVS_COAP_OPT_BLOCK1)) {
            return PROCESS_INITIAL_INVALID_REQUEST;
        }
    }
    server->request_identity = avs_coap_msg_get_identity(msg);

    assert(!is_server_reset(server));
    return PROCESS_INITIAL_OK;
}

static int receive_request(coap_server_t *server) {
    int result = _anjay_coap_in_get_next_message(
            &server->common.in, server->common.coap_ctx, server->common.socket);
    if (result == AVS_COAP_CTX_ERR_MSG_TOO_LONG) {
        const avs_coap_msg_t *partial_msg =
                (avs_coap_msg_t *) server->common.in.buffer;
        /**
         * Due to Size1 Option semantics being not clear enough we don't
         * inform Server about supported message size.
         */
        avs_coap_ctx_send_error(server->common.coap_ctx, server->common.socket,
                                partial_msg,
                                AVS_COAP_CODE_REQUEST_ENTITY_TOO_LARGE);
    }

    if (result) {
        return result;
    }

    const avs_coap_msg_t *msg = _anjay_coap_in_get_message(&server->common.in);
    switch (process_initial_request(server, msg)) {
    case PROCESS_INITIAL_INVALID_REQUEST:
        if (!server->last_error_code) {
            if (avs_coap_msg_get_type(msg) == AVS_COAP_MSG_CONFIRMABLE) {
                avs_coap_ctx_send_empty(server->common.coap_ctx,
                                        server->common.socket,
                                        AVS_COAP_MSG_RESET,
                                        avs_coap_msg_get_id(msg));
            }
        } else {
            avs_coap_ctx_send_error(server->common.coap_ctx,
                                    server->common.socket, msg,
                                    server->last_error_code);
        }
        return -1;
    case PROCESS_INITIAL_OK:
        return 0;
    }

    AVS_UNREACHABLE("invalid enum value");
    return -1;
}

int _anjay_coap_server_get_or_receive_msg(coap_server_t *server,
                                          const avs_coap_msg_t **out_msg) {
    if (server->state == COAP_SERVER_STATE_RESET) {
        int result = receive_request(server);
        if (result) {
            *out_msg = NULL;
            return result;
        }
    }

    assert(server->state != COAP_SERVER_STATE_RESET);
    *out_msg = _anjay_coap_in_get_message(&server->common.in);
    return 0;
}

#ifdef WITH_BLOCK_RECEIVE
static bool blocks_equal(const avs_coap_block_info_t *a,
                         const avs_coap_block_info_t *b) {
    assert(a->valid);
    assert(b->valid);

    return a->size == b->size && a->has_more == b->has_more
           && a->seq_num == b->seq_num;
}

static int block_validate_critical_options(AVS_LIST(coap_block_optbuf_t) opts,
                                           const avs_coap_msg_t *msg,
                                           uint32_t optnum_to_ignore) {
#    define BVCO_LOG_MSG \
        "critical options mismatch when receiving BLOCK request; "
#    define BVCO_LOG_OPT "%" PRIu32 " length %" PRIu32
    AVS_LIST(coap_block_optbuf_t) optbuf = opts;
    for (avs_coap_opt_iterator_t optit = avs_coap_opt_begin(msg);
         !avs_coap_opt_end(&optit);
         avs_coap_opt_next(&optit)) {
        uint32_t optnum = avs_coap_opt_number(&optit);
        if (optnum == optnum_to_ignore || !is_opt_critical(optnum)) {
            continue;
        }
        uint32_t length = avs_coap_opt_content_length(optit.curr_opt);
        if (!optbuf) {
            anjay_log(DEBUG, BVCO_LOG_MSG "expected end; got " BVCO_LOG_OPT,
                      optnum, length);
            return -1;
        }
        if (optnum != optbuf->optnum || length != optbuf->length
                || memcmp(avs_coap_opt_value(optit.curr_opt), optbuf->content,
                          optbuf->length)
                               != 0) {
            anjay_log(DEBUG,
                      BVCO_LOG_MSG "expected " BVCO_LOG_OPT
                                   "; got " BVCO_LOG_OPT,
                      optbuf->optnum, optbuf->length, optnum, length);
            return -1;
        }
        AVS_LIST_ADVANCE(&optbuf);
    }
    if (optbuf) {
        anjay_log(DEBUG, BVCO_LOG_MSG "expected " BVCO_LOG_OPT "; got end",
                  optbuf->optnum, optbuf->length);
        return -1;
    }
    return 0;
#    undef BVCO_LOG_OPT
#    undef BVCO_LOG_MSG
}

typedef enum process_block_result {
    // next block-wise transfer message received
    PROCESS_BLOCK_OK,
    // duplicate of a last block received
    PROCESS_BLOCK_DUPLICATE,
    // received an unrelated packet; reject it and wait for another
    PROCESS_BLOCK_REJECT_CONTINUE,
    // received an invalid block packet; reject it and abort block transfer
    PROCESS_BLOCK_REJECT_ABORT,
} process_block_result_t;

static int retrieve_block_options(const avs_coap_msg_t *msg,
                                  avs_coap_block_info_t *out_block1,
                                  avs_coap_block_info_t *out_block2) {
    int result = 0;

    if (avs_coap_get_block_info(msg, AVS_COAP_BLOCK1, out_block1)) {
        coap_log(DEBUG, "block-wise transfer - BLOCK1 invalid");
        result = -1;
    }

    if (avs_coap_get_block_info(msg, AVS_COAP_BLOCK2, out_block2)) {
        coap_log(DEBUG, "block-wise transfer - BLOCK2 invalid");
        result = -1;
    }

    return result;
}

static process_block_result_t process_next_block(coap_server_t *server,
                                                 const avs_coap_msg_t *msg,
                                                 uint8_t *out_error_code) {
    if (!avs_coap_msg_is_request(msg)) {
        *out_error_code = 0;
        return PROCESS_BLOCK_REJECT_CONTINUE;
    }

    avs_coap_block_info_t new_block;
    avs_coap_block_info_t block2;

    if (retrieve_block_options(msg, &new_block, &block2)) {
        // malformed block option(s)
        *out_error_code = AVS_COAP_CODE_BAD_REQUEST;
        return PROCESS_BLOCK_REJECT_ABORT;
    }

    if (!new_block.valid) {
        coap_log(DEBUG, "block-wise transfer - BLOCK1 missing");
        *out_error_code = AVS_COAP_CODE_SERVICE_UNAVAILABLE;
        return PROCESS_BLOCK_REJECT_CONTINUE;
    }

    if (block2.valid) {
        coap_log(DEBUG, "block-wise transfer - got BLOCK2 option while "
                        "BLOCK1 transfer, this is not implemented");
        *out_error_code = AVS_COAP_CODE_BAD_OPTION;
        return PROCESS_BLOCK_REJECT_ABORT;
    }

    uint32_t offset = get_block_offset(&new_block);
    uint32_t expected_offset =
            get_block_offset(&server->curr_block) + server->curr_block.size;
    avs_coap_msg_identity_t msg_identity = avs_coap_msg_get_identity(msg);

    if (offset != expected_offset) {
        if (avs_coap_identity_equal(&server->request_identity, &msg_identity)
                && blocks_equal(&server->curr_block, &new_block)) {
            return PROCESS_BLOCK_DUPLICATE;
        }

        coap_log(ERROR, "incomplete block request");
        *out_error_code = AVS_COAP_CODE_REQUEST_ENTITY_INCOMPLETE;
        return PROCESS_BLOCK_REJECT_ABORT;
    }

    if (block_validate_critical_options(server->expected_block_opts, msg,
                                        AVS_COAP_OPT_BLOCK1)) {
        *out_error_code = AVS_COAP_CODE_SERVICE_UNAVAILABLE;
        return PROCESS_BLOCK_REJECT_CONTINUE;
    }

    server->state = COAP_SERVER_STATE_HAS_BLOCK1_REQUEST;
    server->curr_block = new_block;
    coap_log(TRACE, "got block: %" PRIu32 " (size %" PRIu16 ")",
             get_block_offset(&new_block), new_block.size);
    return PROCESS_BLOCK_OK;
}

static int send_continue(coap_server_t *server,
                         const avs_coap_msg_identity_t *id) {
    assert(server);
    assert(id);
    assert(server->curr_block.type == AVS_COAP_BLOCK1);

    const avs_coap_msg_t *msg = NULL;
    avs_coap_msg_info_t info = avs_coap_msg_info_init();
    anjay_msg_details_t details = {
        .msg_type = AVS_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = AVS_COAP_CODE_CONTINUE,
        .format = AVS_COAP_FORMAT_NONE
    };

    if (_anjay_coap_common_fill_msg_info(&info, &details, id,
                                         &server->curr_block)) {
        return -1;
    }

    int result = -1;
    size_t storage_size = avs_coap_msg_info_get_storage_size(&info);
    void *storage = avs_malloc(storage_size);
    if (!storage) {
        goto cleanup_info;
    }

    msg = avs_coap_msg_build_without_payload(
            avs_coap_ensure_aligned_buffer(storage), storage_size, &info);
    if (msg) {
        result = avs_coap_ctx_send(server->common.coap_ctx,
                                   server->common.socket, msg);
    }

    avs_free(storage);
cleanup_info:
    avs_coap_msg_info_reset(&info);
    return result;
}

static int receive_next_block(const avs_coap_msg_t *msg,
                              void *server_,
                              bool *out_wait_for_next,
                              uint8_t *out_error_code) {
    assert(msg);

    coap_server_t *server = (coap_server_t *) server_;

    assert(server->state == COAP_SERVER_STATE_NEEDS_NEXT_BLOCK);
    assert(server->curr_block.valid);

    process_block_result_t result =
            process_next_block(server, msg, out_error_code);

    switch (result) {
    case PROCESS_BLOCK_REJECT_CONTINUE:
        *out_wait_for_next = true;
        break;
    case PROCESS_BLOCK_OK:
    case PROCESS_BLOCK_DUPLICATE:
    case PROCESS_BLOCK_REJECT_ABORT:
        server->request_identity = avs_coap_msg_get_identity(msg);
        *out_wait_for_next = false;
        break;
    }

    return (int) result;
}

static int receive_next_block_with_timeout(coap_server_t *server) {
    /**
     * See CoAP BLOCK, 2.5 "Using the Block1 Option".
     *
     * That's a *really* big timeout, but CoAP BLOCK spec suggests that value
     * to be used as a timeout until cached state can be discarded.
     */
    avs_coap_tx_params_t tx_params =
            avs_coap_ctx_get_tx_params(server->common.coap_ctx);
    avs_time_duration_t timeout = avs_coap_exchange_lifetime(&tx_params);
    while (avs_time_duration_less(AVS_TIME_DURATION_ZERO, timeout)) {
        int recv_result = -1;
        int result = _anjay_coap_common_recv_msg_with_timeout(
                server->common.coap_ctx, server->common.socket,
                &server->common.in, &timeout, receive_next_block, server,
                &recv_result);
        if (result) {
            return result;
        }

        switch (recv_result) {
        case PROCESS_BLOCK_DUPLICATE:
            send_continue(server, &server->request_identity);
            break;

        case PROCESS_BLOCK_OK:
            assert(server->state == COAP_SERVER_STATE_HAS_BLOCK1_REQUEST);
            return -(server->state != COAP_SERVER_STATE_HAS_BLOCK1_REQUEST);

        case PROCESS_BLOCK_REJECT_CONTINUE:
        default:
            AVS_UNREACHABLE("should never happen");
            return -1;

        case PROCESS_BLOCK_REJECT_ABORT:
            return recv_result;
        }
    }
    coap_log(DEBUG,
             "timeout reached while waiting for block (offset = %" PRIu32 ")",
             get_block_offset(&server->curr_block));
    return -1;
}
#endif // WITH_BLOCK_RECEIVE

int _anjay_coap_server_read(coap_server_t *server,
                            size_t *out_bytes_read,
                            char *out_message_finished,
                            void *buffer,
                            size_t buffer_length) {
    if (is_server_reset(server)) {
        return -1;
    }

#ifdef WITH_BLOCK_RECEIVE
    if (server->state == COAP_SERVER_STATE_NEEDS_NEXT_BLOCK) {
        // An attempt to read more payload was made, but we finished reading
        // last packet. Send 2.31 Continue to let the server know we are ready
        // to handle the next block and wait for it.
        send_continue(server, _anjay_coap_server_get_request_identity(server));

        int result = receive_next_block_with_timeout(server);
        if (result) {
            return result;
        }
    }
#endif

    _anjay_coap_in_read(&server->common.in, out_bytes_read,
                        out_message_finished, buffer, buffer_length);

    if (*out_message_finished
            && server->state == COAP_SERVER_STATE_HAS_BLOCK1_REQUEST) {
        if (server->curr_block.has_more) {
#ifdef WITH_BLOCK_RECEIVE
            coap_log(TRACE, "block: packet %" PRIu32 " finished",
                     server->curr_block.seq_num);

            server->state = COAP_SERVER_STATE_NEEDS_NEXT_BLOCK;
            *out_message_finished = false;

            // Even though we return the rest of packet payload, we must not
            // send the 2.31 Continue response yet: the payload might be
            // malformed and cause an error response, terminating the block-wise
            // transfer. If we sent the Continue, it would result in two
            // different responses to the same server request, which is quite
            // disastrous.
#else
            coap_log(ERROR, "block: Block1 requests not supported");
            return -1;
#endif
        } else {
            coap_log(TRACE, "block: read complete");
        }
    }

    return 0;
}

#ifdef WITH_BLOCK_SEND
static int
block_write(coap_server_t *server, const void *data, size_t data_length) {
    if (!server->block_ctx) {
        uint16_t block_size =
                (uint16_t) (server->curr_block.valid
                                    ? server->curr_block.size
                                    : AVS_COAP_MSG_BLOCK_MAX_SIZE);

        server->static_id_source = _anjay_coap_id_source_new_static(
                _anjay_coap_server_get_request_identity(server));
        if (!server->static_id_source) {
            return -1;
        }
        server->block_ctx = _anjay_coap_block_response_new(
                block_size, &server->common, server->static_id_source,
                &server->block_relation_validator);

        if (!server->block_ctx) {
            _anjay_coap_id_source_release(&server->static_id_source);
            return -1;
        }
    }
    int result = _anjay_coap_block_transfer_write(server->block_ctx, data,
                                                  data_length);
    if (result) {
        server->request_identity =
                _anjay_coap_block_response_last_request_id(server->block_ctx);
        _anjay_coap_block_transfer_delete(&server->block_ctx);
        _anjay_coap_id_source_release(&server->static_id_source);
    }
    return result;
}
#else
#    define block_write(...) \
        (coap_log(ERROR, "sending blockwise responses not supported"), -1)
#endif

static bool block_response_requested(coap_server_t *server) {
    return server->curr_block.valid
           && server->curr_block.type == AVS_COAP_BLOCK2;
}

int _anjay_coap_server_write(coap_server_t *server,
                             const void *data,
                             size_t data_length) {
    size_t bytes_written = 0;
    if (!has_block_ctx(server) && !block_response_requested(server)) {
        bytes_written =
                _anjay_coap_out_write(&server->common.out, data, data_length);
        if (bytes_written == data_length) {
            return 0;
        } else {
            coap_log(TRACE, "response payload does not fit in the buffer "
                            "- initiating block-wise transfer");
        }
    }

    return block_write(server, (const uint8_t *) data + bytes_written,
                       data_length - bytes_written);
}
