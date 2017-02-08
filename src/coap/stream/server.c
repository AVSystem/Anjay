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

#define ANJAY_COAP_STREAM_INTERNALS

#include "server.h"

#include <anjay_modules/time.h>

#include <inttypes.h>

#include "../log.h"
#include "../utils.h"
#include "../msg_internal.h"
#include "../id_source/static.h"
#include "stream.h"

VISIBILITY_SOURCE_BEGIN

#ifdef WITH_BLOCK_SEND
#define has_block_ctx(server) ((server)->block_ctx)
#else
#define has_block_ctx(server) (false)
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
}

const anjay_coap_msg_identity_t *
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
    uint8_t cls = _anjay_coap_msg_code_get_class(&msg_code);
    return cls == 2;
}

int _anjay_coap_server_setup_response(coap_server_t *server,
                                      coap_output_buffer_t *out,
                                      anjay_coap_socket_t *socket,
                                      const anjay_msg_details_t *details) {
    if (is_server_reset(server)) {
        coap_log(DEBUG, "no request to respond to");
        return -1;
    }

    if (has_error(server)) {
        coap_log(WARNING, "setup_response called with unsent error: %s",
                 ANJAY_COAP_CODE_STRING(server->last_error_code));
        clear_error(server);
    }
    if (!_anjay_coap_out_is_reset(out)) {
        coap_log(WARNING, "setup_response called, but out buffer not reset");
        _anjay_coap_out_reset(out);
    }

    const coap_block_info_t *block = NULL;
    if (is_block1_transfer(server)
            && is_success_response(details->msg_code)) {
        block = &server->curr_block;
    }

    _anjay_coap_out_setup_mtu(out, socket);
    return _anjay_coap_out_setup_msg(out, &server->request_identity,
                                     details, block);
}

void _anjay_coap_server_set_error(coap_server_t *server, uint8_t code) {
    if (has_error(server)) {
        coap_log(DEBUG, "error %s skipped (%s still not sent)",
                 ANJAY_COAP_CODE_STRING(code),
                 ANJAY_COAP_CODE_STRING(server->last_error_code));
    }

    server->last_error_code = code;
    coap_log(DEBUG, "server error set to %s", ANJAY_COAP_CODE_STRING(code));
}

static void setup_error_response(coap_server_t *server,
                                 coap_output_buffer_t *out,
                                 anjay_coap_socket_t *socket) {
    assert(has_error(server));

    const anjay_msg_details_t details = {
        .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = server->last_error_code,
        .format = ANJAY_COAP_FORMAT_NONE
    };

    _anjay_coap_out_reset(out);
    int result = _anjay_coap_server_setup_response(server, out, socket,
                                                   &details);
    assert(result == 0);

    (void)result;
}

int _anjay_coap_server_finish_response(coap_server_t *server,
                                       coap_output_buffer_t *out,
                                       anjay_coap_socket_t *socket) {
    if (has_error(server)) {
        setup_error_response(server, out, socket);
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
        result = _anjay_coap_out_update_msg_header(
                out, &server->request_identity, &server->curr_block);
    }

    if (!result) {
        const anjay_coap_msg_t *msg = _anjay_coap_out_build_msg(out);
        result = _anjay_coap_socket_send(socket, msg);
    }
    return result;
}

static bool is_opt_critical(uint32_t opt_number) {
    return opt_number % 2;
}

static int block_store_critical_options(AVS_LIST(coap_block_optbuf_t) *out,
                                        const anjay_coap_msg_t *msg,
                                        uint32_t optnum_to_ignore) {
    AVS_LIST(coap_block_optbuf_t) *outptr = out;
    assert(!*outptr);

    for (anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);
            !_anjay_coap_opt_end(&optit); _anjay_coap_opt_next(&optit)) {
        uint32_t optnum = _anjay_coap_opt_number(&optit);
        if (optnum == optnum_to_ignore || !is_opt_critical(optnum)) {
            continue;
        }
        uint32_t length = _anjay_coap_opt_content_length(optit.curr_opt);
        *outptr = (AVS_LIST(coap_block_optbuf_t)) AVS_LIST_NEW_BUFFER(
                offsetof(coap_block_optbuf_t, content) + length);
        if (!*outptr) {
            goto err;
        }
        (*outptr)->optnum = optnum;
        (*outptr)->length = length;
        memcpy((*outptr)->content, _anjay_coap_opt_value(optit.curr_opt),
                length);
        outptr = AVS_LIST_NEXT_PTR(outptr);
    }
    return 0;
err:
    AVS_LIST_CLEAR(out);
    return -1;
}

typedef enum process_result {
    /** The message is a correct request, a basic one or the first BLOCK */
    PROCESS_INITIAL_OK,

    /** Not a valid request message. last_error_code may be set to enforce a
     * particular response code. */
    PROCESS_INITIAL_INVALID_REQUEST,
} process_result_t;

static process_result_t process_initial_request(coap_server_t *server,
                                                const anjay_coap_msg_t *msg) {
    assert(is_server_reset(server));

    anjay_coap_msg_type_t type = _anjay_coap_msg_header_get_type(&msg->header);
    if (!_anjay_coap_msg_is_request(msg)
            // incoming Reset may still require some kind of reaction,
            // so it should be handled by upper layers
            && type != ANJAY_COAP_MSG_RESET) {
        coap_log(DEBUG, "invalid request: %s",
                 ANJAY_COAP_CODE_STRING(msg->header.code));
        return PROCESS_INITIAL_INVALID_REQUEST;
    }

    coap_block_info_t block1;
    coap_block_info_t block2;
    int result1 = _anjay_coap_common_get_block_info(msg, COAP_BLOCK1, &block1);
    int result2 = _anjay_coap_common_get_block_info(msg, COAP_BLOCK2, &block2);
    if (result1 || result2) {
        _anjay_coap_server_set_error(server, -ANJAY_ERR_BAD_REQUEST);
        return PROCESS_INITIAL_INVALID_REQUEST;
    }
    /**
     * CoAP supports bidirectional block communication, but LWM2M does not have
     * any operation for which it would be useful. Therefore it's not implemented.
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
        coap_log(TRACE, "block request: %u, size %u",
                 get_block_offset(&server->curr_block),
                 server->curr_block.size);

        if (server->curr_block.seq_num != 0) {
            coap_log(ERROR, "initial block seq_num nonzero");
            _anjay_coap_server_set_error(server,
                                         -ANJAY_ERR_REQUEST_ENTITY_INCOMPLETE);
            return PROCESS_INITIAL_INVALID_REQUEST;
        }

        if (block1.valid
                && block_store_critical_options(&server->expected_block_opts, msg,
                                                ANJAY_COAP_OPT_BLOCK1)) {
            return PROCESS_INITIAL_INVALID_REQUEST;
        }
    }
    server->request_identity = _anjay_coap_common_identity_from_msg(msg);

    assert(!is_server_reset(server));
    return PROCESS_INITIAL_OK;
}

static int receive_request(coap_server_t *server,
                           coap_input_buffer_t *in,
                           anjay_coap_socket_t *socket) {
    int result = _anjay_coap_in_get_next_message(in, socket);
    if (result == ANJAY_COAP_SOCKET_RECV_ERR_MSG_TOO_LONG) {
        const anjay_coap_msg_t *partial_msg = (anjay_coap_msg_t *) in->buffer;
        if (partial_msg->length < sizeof(partial_msg->header)) {
            coap_log(ERROR, "message too small to read header properly");
            return result;
        }

        size_t token_size =
                _anjay_coap_msg_header_get_token_length(&partial_msg->header);
        if (partial_msg->length < sizeof(partial_msg->header) + token_size) {
            coap_log(ERROR, "message too small to read token properly");
            return result;
        }
        /**
         * Due to Size1 Option semantics being not clear enough we don't
         * inform Server about supported message size.
         */
        _anjay_coap_common_send_error(socket, partial_msg,
                                      ANJAY_COAP_CODE_REQUEST_ENTITY_TOO_LARGE);
    }

    if (result) {
        return result;
    }

    const anjay_coap_msg_t *msg = _anjay_coap_in_get_message(in);
    switch (process_initial_request(server, msg)) {
    case PROCESS_INITIAL_INVALID_REQUEST:
        if (!server->last_error_code) {
            _anjay_coap_common_reject_message(socket, msg);
        } else {
            _anjay_coap_common_send_error(socket, msg, server->last_error_code);
        }
        return -1;
    case PROCESS_INITIAL_OK:
        return 0;
    }

    assert(0 && "invalid enum value");
    return -1;
}

int _anjay_coap_server_get_or_receive_msg(coap_server_t *server,
                                          coap_input_buffer_t *in,
                                          anjay_coap_socket_t *socket,
                                          const anjay_coap_msg_t **out_msg) {
    if (server->state == COAP_SERVER_STATE_RESET) {
        int result = receive_request(server, in, socket);
        if (result) {
            *out_msg = NULL;
            return result;
        }
    }

    assert(server->state != COAP_SERVER_STATE_RESET);
    *out_msg = _anjay_coap_in_get_message(in);
    return 0;
}

#ifdef WITH_BLOCK_RECEIVE
static uint32_t get_block_offset(const coap_block_info_t *block) {
    assert(_anjay_coap_is_valid_block_size(block->size));

    return block->seq_num * block->size;
}

static bool blocks_equal(const coap_block_info_t *a,
                         const coap_block_info_t *b) {
    assert(a->valid);
    assert(b->valid);

    return a->size == b->size
        && a->has_more == b->has_more
        && a->seq_num == b->seq_num;
}

static int block_validate_critical_options(AVS_LIST(coap_block_optbuf_t) opts,
                                           const anjay_coap_msg_t *msg,
                                           uint32_t optnum_to_ignore) {
#define BVCO_LOG_MSG "critical options mismatch when receiving BLOCK request; "
#define BVCO_LOG_OPT "%" PRIu32 " length %" PRIu32
    AVS_LIST(coap_block_optbuf_t) optbuf = opts;
    for (anjay_coap_opt_iterator_t optit = _anjay_coap_opt_begin(msg);
            !_anjay_coap_opt_end(&optit); _anjay_coap_opt_next(&optit)) {
        uint32_t optnum = _anjay_coap_opt_number(&optit);
        if (optnum == optnum_to_ignore || !is_opt_critical(optnum)) {
            continue;
        }
        uint32_t length = _anjay_coap_opt_content_length(optit.curr_opt);
        if (!optbuf) {
            anjay_log(DEBUG, BVCO_LOG_MSG "expected end; got " BVCO_LOG_OPT,
                      optnum, length);
            return -1;
        }
        if (optnum != optbuf->optnum
                || length != optbuf->length
                || memcmp(_anjay_coap_opt_value(optit.curr_opt),
                          optbuf->content, optbuf->length) != 0) {
            anjay_log(DEBUG, BVCO_LOG_MSG
                             "expected " BVCO_LOG_OPT "; got " BVCO_LOG_OPT,
                      optbuf->optnum, optbuf->length, optnum, length);
            return -1;
        }
        optbuf = AVS_LIST_NEXT(optbuf);
    }
    if (optbuf) {
        anjay_log(DEBUG, BVCO_LOG_MSG "expected " BVCO_LOG_OPT "; got end",
                  optbuf->optnum, optbuf->length);
        return -1;
    }
    return 0;
#undef BVCO_LOG_OPT
#undef BVCO_LOG_MSG
}

#define PROCESS_BLOCK_INVALID (-1)
#define PROCESS_BLOCK_OK 0
#define PROCESS_BLOCK_DUPLICATE 1

/**
 * Returns: any PROCESS_BLOCK_* ANJAY_ERR_* constant
 */
static int process_next_block(coap_server_t *server,
                              const anjay_coap_msg_t *msg) {
    coap_block_info_t new_block;
    coap_block_info_t block2;
    int result =
            _anjay_coap_common_get_block_info(msg, COAP_BLOCK1, &new_block);
    if (!new_block.valid) {
        coap_log(DEBUG, "block-wise transfer - rejecting message: BLOCK1 %s",
                 result ? "invalid" : "missing");
        if (result) {
            return ANJAY_ERR_BAD_REQUEST;
        } else {
            return PROCESS_BLOCK_INVALID;
        }
    }

    if (_anjay_coap_common_get_block_info(msg, COAP_BLOCK2, &block2)) {
        coap_log(DEBUG, "block-wise transfer - cannot get information about "
                        "BLOCK2 option");
        return PROCESS_BLOCK_INVALID;
    } else if (block2.valid) {
        coap_log(DEBUG, "block-wise transfer - got BLOCK2 option while "
                        "BLOCK1 transfer, this is not implemented");
        return ANJAY_ERR_BAD_OPTION;
    }

    uint32_t offset = get_block_offset(&new_block);
    uint32_t expected_offset =
            get_block_offset(&server->curr_block) + server->curr_block.size;
    anjay_coap_msg_identity_t msg_identity = _anjay_coap_common_identity_from_msg(msg);

    if (offset != expected_offset) {
        if (_anjay_coap_common_identity_equal(&server->request_identity,
                                              &msg_identity)
                && blocks_equal(&server->curr_block, &new_block)) {
            return PROCESS_BLOCK_DUPLICATE;
        }

        coap_log(ERROR, "incomplete block request");
        return ANJAY_ERR_REQUEST_ENTITY_INCOMPLETE;
    }

    if (block_validate_critical_options(server->expected_block_opts, msg,
                                        ANJAY_COAP_OPT_BLOCK1)) {
        return PROCESS_BLOCK_INVALID;
    }

    server->state = COAP_SERVER_STATE_HAS_BLOCK1_REQUEST;
    server->curr_block = new_block;
    coap_log(TRACE, "got block: %u (size %u)",
             get_block_offset(&new_block), new_block.size);
    return PROCESS_BLOCK_OK;
}

static int send_continue(anjay_coap_socket_t *socket,
                         const anjay_coap_msg_identity_t *id,
                         const coap_block_info_t *block_info) {
    assert(socket);
    assert(id);
    assert(block_info && block_info->type == COAP_BLOCK1);

    anjay_coap_msg_info_t info = _anjay_coap_msg_info_init();
    anjay_msg_details_t details = {
        .msg_type = ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
        .msg_code = ANJAY_COAP_CODE_CONTINUE,
        .format = ANJAY_COAP_FORMAT_NONE
    };

    if (_anjay_coap_common_fill_msg_info(&info, &details, id, block_info)) {
        return -1;
    }

    int result = -1;
    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info);
    void *storage = malloc(storage_size);
    if (!storage) {
        goto cleanup_info;
    }

    const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(storage),
            storage_size, &info);
    if (msg) {
        result = _anjay_coap_socket_send(socket, msg);
    }

    free(storage);
cleanup_info:
    _anjay_coap_msg_info_reset(&info);
    return result;
}

static int receive_next_block(const anjay_coap_msg_t *msg,
                              void *server_,
                              bool *out_wait_for_next,
                              uint8_t *out_error_code) {
    (void) out_error_code;
    assert(msg);

    coap_server_t *server = (coap_server_t *)server_;

    assert(server->state == COAP_SERVER_STATE_NEEDS_NEXT_BLOCK);
    assert(server->curr_block.valid);

    int result = process_next_block(server, msg);

    switch (result) {
    case PROCESS_BLOCK_INVALID:
        break;
    case PROCESS_BLOCK_OK:
    case PROCESS_BLOCK_DUPLICATE:
    default: // any ANJAY_ERR_*
        *out_wait_for_next = false;
        server->request_identity = _anjay_coap_common_identity_from_msg(msg);
        break;
    }

    return (int)result;
}

static int receive_next_block_with_timeout(coap_server_t *server,
                                           coap_input_buffer_t *in,
                                           anjay_coap_socket_t *socket) {
    /**
     * See CoAP BLOCK, 2.5 "Using the Block1 Option".
     *
     * That's a *really* big timeout, but CoAP BLOCK spec suggests that value
     * to be used as a timeout until cached state can be discarded.
     */
    int32_t timeout_ms =
            _anjay_coap_exchange_lifetime_ms(&in->transmission_params);
    while (timeout_ms > 0) {
        int recv_result = -1;
        int result = _anjay_coap_common_recv_msg_with_timeout(
                socket, in, &timeout_ms, receive_next_block, server,
                &recv_result);
        if (result) {
            return result;
        }

        switch (recv_result) {
        case PROCESS_BLOCK_DUPLICATE:
            {
                const anjay_coap_msg_identity_t *id = &server->request_identity;
                send_continue(socket, id, &server->curr_block);
            }
            break;

        case PROCESS_BLOCK_OK:
            assert(server->state == COAP_SERVER_STATE_HAS_BLOCK1_REQUEST);
            return -(server->state != COAP_SERVER_STATE_HAS_BLOCK1_REQUEST);

        case PROCESS_BLOCK_INVALID:
            assert(0 && "should never happen");
            return -1;

        default: // any ANJAY_ERR_*
            return recv_result;
        }
    }
    coap_log(DEBUG, "timeout reached while waiting for block (offset = %u)",
             get_block_offset(&server->curr_block));
    return -1;
}
#endif // WITH_BLOCK_RECEIVE

int _anjay_coap_server_read(coap_server_t *server,
                            coap_input_buffer_t *in,
                            anjay_coap_socket_t *socket,
                            size_t *out_bytes_read,
                            char *out_message_finished,
                            void *buffer,
                            size_t buffer_length) {
    if (is_server_reset(server)) {
        return -1;
    }

#ifdef WITH_BLOCK_RECEIVE
    if (server->state == COAP_SERVER_STATE_NEEDS_NEXT_BLOCK) {
        int result = receive_next_block_with_timeout(server, in, socket);
        if (result) {
            return result;
        }
    }
#endif

    _anjay_coap_in_read(in, out_bytes_read, out_message_finished,
                        buffer, buffer_length);

    if (*out_message_finished
            && server->state == COAP_SERVER_STATE_HAS_BLOCK1_REQUEST) {
        if (server->curr_block.has_more) {
#ifdef WITH_BLOCK_RECEIVE
            coap_log(TRACE, "block: packet %u finished",
                     server->curr_block.seq_num);

            server->state = COAP_SERVER_STATE_NEEDS_NEXT_BLOCK;

            const anjay_coap_msg_identity_t *id =
                    _anjay_coap_server_get_request_identity(server);
            send_continue(socket, id, &server->curr_block);

            *out_message_finished = false;
#else
            (void) socket;
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
static int block_write(coap_server_t *server,
                       coap_input_buffer_t *in,
                       coap_output_buffer_t *out,
                       anjay_coap_socket_t *socket,
                       const void *data,
                       size_t data_length) {
    if (!server->block_ctx) {
        uint16_t block_size = server->curr_block.valid
                ? server->curr_block.size
                : ANJAY_COAP_MSG_BLOCK_MAX_SIZE;

        server->static_id_source = _anjay_coap_id_source_new_static(
                _anjay_coap_server_get_request_identity(server));
        if (!server->static_id_source) {
            return -1;
        }
        server->block_ctx =
                _anjay_coap_block_response_new(block_size, in, out, socket,
                                               server->static_id_source);

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
#define block_write(...) \
        (coap_log(ERROR, "sending blockwise responses not supported"), -1)
#endif

static bool block_response_requested(coap_server_t *server) {
    return server->curr_block.valid && server->curr_block.type == COAP_BLOCK2;
}

int _anjay_coap_server_write(coap_server_t *server,
                             coap_input_buffer_t *in,
                             coap_output_buffer_t *out,
                             anjay_coap_socket_t *socket,
                             const void *data,
                             size_t data_length) {
    (void) in; (void) socket;
    size_t bytes_written = 0;
    if (!has_block_ctx(server) && !block_response_requested(server)) {
        bytes_written = _anjay_coap_out_write(out, data, data_length);
        if (bytes_written == data_length) {
            return 0;
        } else {
            coap_log(TRACE, "response payload does not fit in the buffer "
                     "- initiating block-wise transfer");
        }
    }

    return block_write(server, in, out, socket,
                       (const uint8_t*)data + bytes_written,
                       data_length - bytes_written);
}
