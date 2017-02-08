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

#include "stream.h"

#include <avsystem/commons/stream/net.h>
#include <avsystem/commons/stream_v_table.h>

#include "../log.h"

#include "../id_source/auto.h"

VISIBILITY_SOURCE_BEGIN

static coap_client_t *get_client(coap_stream_t *stream) {
    assert(stream->state == STREAM_STATE_CLIENT);
    return &stream->state_data.client;
}

static coap_server_t *get_server(coap_stream_t *stream) {
    assert(stream->state == STREAM_STATE_SERVER);
    return &stream->state_data.server;
}

static void reset(coap_stream_t *stream) {
    _anjay_coap_in_reset(&stream->in);
    _anjay_coap_out_reset(&stream->out);

    switch (stream->state) {
    case STREAM_STATE_CLIENT:
        _anjay_coap_client_reset(get_client(stream));
        break;
    case STREAM_STATE_SERVER:
        _anjay_coap_server_reset(get_server(stream));
        break;
    default:
        break;
    }

    stream->state = STREAM_STATE_IDLE;
    coap_log(TRACE, "stream: IDLE mode (reset)");
}

static bool is_reset(coap_stream_t *stream) {
    bool is_idle = stream->state == STREAM_STATE_IDLE;

    assert(!is_idle || _anjay_coap_in_is_reset(&stream->in));
    assert(!is_idle || _anjay_coap_out_is_reset(&stream->out));

    return is_idle;
}

static void become_server(coap_stream_t *stream) {
    assert(stream->state == STREAM_STATE_IDLE);

    reset(stream);
    stream->state = STREAM_STATE_SERVER;
    coap_log(TRACE, "stream: SERVER mode");

    _anjay_coap_server_reset(get_server(stream));
}

static void become_client(coap_stream_t *stream) {
    assert(stream->state == STREAM_STATE_IDLE);

    reset(stream);
    stream->state = STREAM_STATE_CLIENT;
    coap_log(TRACE, "stream: CLIENT mode");

    _anjay_coap_client_reset(get_client(stream));
}

static const anjay_coap_msg_t *get_or_receive_msg(coap_stream_t *stream) {
    const anjay_coap_msg_t *msg = NULL;

    switch (stream->state) {
    case STREAM_STATE_CLIENT:
        if (_anjay_coap_client_get_or_receive_msg(get_client(stream),
                                                  &stream->in, stream->socket,
                                                  &msg)) {
            reset(stream);
            msg = NULL;
        }
        break;
    case STREAM_STATE_IDLE:
        coap_log(TRACE, "get_or_receive_msg: idle stream, receiving");
        become_server(stream);
        // fall-through
    case STREAM_STATE_SERVER:
        if (_anjay_coap_server_get_or_receive_msg(get_server(stream),
                                                  &stream->in, stream->socket,
                                                  &msg)) {
            reset(stream);
            msg = NULL;
        }
        break;
    }

    return msg;
}

static inline void
assert_tx_params_valid(const coap_transmission_params_t *tx_params) {
    (void) tx_params;

    // ACK_TIMEOUT below 1 second would violate the guidelines of [RFC5405].
    // -- RFC 7252, 4.8.1
    assert(tx_params->ack_timeout_ms >= 1000);

    // ACK_RANDOM_FACTOR MUST NOT be decreased below 1.0, and it SHOULD have
    // a value that is sufficiently different from 1.0 to provide some
    // protection from synchronization effects.
    // -- RFC 7252, 4.8.1
    assert(tx_params->ack_random_factor > 1.0);
}

static int setup_response(avs_stream_abstract_t *stream_,
                          const anjay_msg_details_t *details) {
    coap_stream_t *stream = (coap_stream_t*)stream_;

    if (stream->state != STREAM_STATE_SERVER) {
        coap_log(ERROR, "no request to respond to");
        return -1;
    }

    return _anjay_coap_server_setup_response(get_server(stream), &stream->out,
                                             stream->socket, details);
}

static const anjay_coap_stream_ext_t COAP_STREAM_EXT_VTABLE = {
    .setup_response = setup_response
};

static int coap_getsock(avs_stream_abstract_t *stream_,
                        avs_net_abstract_socket_t **out_sock) {
    coap_stream_t *stream = (coap_stream_t*)stream_;
    *out_sock = _anjay_coap_socket_get_backend(stream->socket);
    return *out_sock == NULL ? -1 : 0;
}

static int coap_setsock(avs_stream_abstract_t *stream_,
                        avs_net_abstract_socket_t *sock) {
    coap_stream_t *stream = (coap_stream_t*)stream_;
    if (!is_reset(stream)) {
        return -1;
    }

    if (_anjay_coap_socket_get_backend(stream->socket) != NULL
            && sock != NULL) {
        assert(0 && "swapping socket on an not-yet-released stream");
        return -1;
    }

    _anjay_coap_socket_set_backend(stream->socket, sock);
    return 0;
}

static const avs_stream_v_table_extension_net_t NET_EXT_VTABLE = {
    coap_getsock,
    coap_setsock
};

static const avs_stream_v_table_extension_t COAP_STREAM_EXT[] = {
    { ANJAY_COAP_STREAM_EXTENSION, &COAP_STREAM_EXT_VTABLE },
    { AVS_STREAM_V_TABLE_EXTENSION_NET, &NET_EXT_VTABLE },
    AVS_STREAM_V_TABLE_EXTENSION_NULL
};

static int coap_write(avs_stream_abstract_t *stream_,
                      const void *data,
                      size_t data_length) {
    coap_stream_t *stream = (coap_stream_t *)stream_;

    switch (stream->state) {
    case STREAM_STATE_CLIENT:
        return _anjay_coap_client_write(get_client(stream), &stream->in,
                                        &stream->out, stream->socket,
                                        stream->id_source, data, data_length);
    case STREAM_STATE_SERVER:
        return _anjay_coap_server_write(get_server(stream), &stream->in,
                                        &stream->out, stream->socket,
                                        data, data_length);
    default:
        coap_log(ERROR, "write called on an IDLE stream");
        return -1;
    }
}

static int coap_finish_message(avs_stream_abstract_t *stream_) {
    coap_stream_t *stream = (coap_stream_t*)stream_;

    switch (stream->state) {
    case STREAM_STATE_CLIENT:
         return _anjay_coap_client_finish_request(get_client(stream),
                                                  &stream->in, &stream->out,
                                                  stream->socket);

    case STREAM_STATE_SERVER:
        return _anjay_coap_server_finish_response(get_server(stream),
                                                  &stream->out, stream->socket);

    default:
        coap_log(ERROR, "finish_message called on an IDLE stream");
        return -1;
    }
}

static int coap_read(avs_stream_abstract_t *stream_,
                     size_t *out_bytes_read,
                     char *out_message_finished,
                     void *buffer,
                     size_t buffer_length) {
    coap_stream_t *stream = (coap_stream_t *)stream_;
    assert(stream->in.buffer);

    int result = -1;

    if (get_or_receive_msg(stream)) {
        switch (stream->state) {
        case STREAM_STATE_IDLE:
            assert(0 && "should never happen");
            break;
        case STREAM_STATE_SERVER:
            result = _anjay_coap_server_read(get_server(stream), &stream->in,
                                             stream->socket, out_bytes_read,
                                             out_message_finished,
                                             buffer, buffer_length);
            break;
        case STREAM_STATE_CLIENT:
            result = _anjay_coap_client_read(get_client(stream), &stream->in,
                                             stream->socket, out_bytes_read,
                                             out_message_finished,
                                             buffer, buffer_length);
            break;
        }
    }

    if (!result && *out_message_finished) {
        _anjay_coap_in_reset(&stream->in);
    }

    return result;
}

static int coap_reset(avs_stream_abstract_t *stream_) {
    reset((coap_stream_t *)stream_);
    return 0;
}

static int coap_close(avs_stream_abstract_t *stream_) {
    coap_stream_t *stream = (coap_stream_t *)stream_;

    reset(stream);

    if (stream->socket) {
        _anjay_coap_socket_cleanup(&stream->socket);
    }

    free(stream->in.buffer);
    stream->in.buffer = NULL;

    free(stream->out.buffer);
    stream->out.buffer = NULL;

    _anjay_coap_id_source_release(&stream->id_source);

    return 0;
}

static int unimplemented() {
    return -1;
}

static const avs_stream_v_table_t COAP_STREAM_VTABLE = {
    coap_write,
    coap_finish_message,
    coap_read,
    (avs_stream_peek_t) unimplemented,
    coap_reset,
    coap_close,
    (avs_stream_errno_t) unimplemented,
    COAP_STREAM_EXT
};

int _anjay_coap_stream_create(avs_stream_abstract_t **stream_,
                              anjay_coap_socket_t *socket,
                              size_t in_buffer_size,
                              size_t out_buffer_size) {
    coap_stream_t *stream = (coap_stream_t *)calloc(1, sizeof(coap_stream_t));
    if (!stream) {
        return -1;
    }

    stream->vtable = &COAP_STREAM_VTABLE;
    stream->socket = socket;

    stream->state = STREAM_STATE_IDLE;

    // buffers must be able to hold whole CoAP message + its length;
    // add a bit of extra space for length so that {in,out}_buffer_size
    // are exact limits for the CoAP message size
    const size_t extra_bytes_required = offsetof(anjay_coap_msg_t, header);
    in_buffer_size += extra_bytes_required;
    out_buffer_size += extra_bytes_required;

    stream->in.buffer_size = in_buffer_size;
    stream->in.buffer = (uint8_t *)malloc(in_buffer_size);
    assert_tx_params_valid(&_anjay_coap_DEFAULT_TX_PARAMS);
    stream->in.transmission_params = _anjay_coap_DEFAULT_TX_PARAMS;
    stream->in.rand_seed = (anjay_rand_seed_t) time(NULL);
    stream->id_source =
            _anjay_coap_id_source_auto_new((anjay_rand_seed_t) time(NULL), 8);
    if (!stream->id_source) {
        free(stream);
        return -1;
    }

    stream->out = _anjay_coap_out_init((uint8_t *) malloc(out_buffer_size),
                                       out_buffer_size);

    if (!stream->in.buffer || !stream->out.buffer) {
        coap_close((avs_stream_abstract_t *) stream);
        free(stream);
        _anjay_coap_id_source_release(&stream->id_source);
        return -1;
    }
    reset(stream);

    *stream_ = (avs_stream_abstract_t *)stream;
    return 0;
}

int _anjay_coap_stream_get_tx_params(
        avs_stream_abstract_t *stream_,
        coap_transmission_params_t *out_tx_params) {
    coap_stream_t *stream = (coap_stream_t*) stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);
    *out_tx_params = stream->in.transmission_params;
    return 0;
}

int _anjay_coap_stream_set_tx_params(
        avs_stream_abstract_t *stream_,
        const coap_transmission_params_t *tx_params) {
    coap_stream_t *stream = (coap_stream_t*) stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);
    assert_tx_params_valid(tx_params);
    stream->in.transmission_params = *tx_params;
    return 0;
}

int _anjay_coap_stream_setup_response(avs_stream_abstract_t *stream,
                                      const anjay_msg_details_t *details) {
    const anjay_coap_stream_ext_t *coap = (const anjay_coap_stream_ext_t *)
            avs_stream_v_table_find_extension(stream,
                                              ANJAY_COAP_STREAM_EXTENSION);
    if (coap) {
        return coap->setup_response(stream, details);
    }
    assert(0 && "`coap' pointer is NULL");
    return -1;
}

int _anjay_coap_stream_setup_request(
        avs_stream_abstract_t *stream_,
        const anjay_msg_details_t *details,
        const anjay_coap_token_t *token,
        size_t token_size) {
    assert(token || token_size == 0);

    coap_stream_t *stream = (coap_stream_t*)stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);
    switch (stream->state) {
    case STREAM_STATE_SERVER:
        coap_log(ERROR, "setup_request called while in SERVER state");
        return -1;

    case STREAM_STATE_CLIENT:
        coap_log(DEBUG, "overwriting previous request");
        reset(stream);
        assert(stream->state == STREAM_STATE_IDLE);
        // fall-through
    case STREAM_STATE_IDLE:
        break;
    }

    become_client(stream);

    anjay_coap_msg_identity_t identity =
            _anjay_coap_id_source_get(stream->id_source);
    if (token) {
        identity.token = *token;
        identity.token_size = token_size;
    }
    return _anjay_coap_client_setup_request(get_client(stream), &stream->out,
                                            stream->socket, details, &identity);
}

int _anjay_coap_stream_set_error(avs_stream_abstract_t *stream_,
                                 uint8_t code) {
    coap_stream_t *stream = (coap_stream_t*)stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);

    if (stream->state != STREAM_STATE_SERVER) {
        coap_log(ERROR, "set_error only makes sense on a server mode stream");
        return -1;
    }

    _anjay_coap_server_set_error(get_server(stream), code);
    return 0;
}

int _anjay_coap_stream_get_code(avs_stream_abstract_t *stream_,
                                uint8_t *out_code) {
    coap_stream_t *stream = (coap_stream_t*)stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);

    const anjay_coap_msg_t *msg = get_or_receive_msg(stream);
    if (!msg) {
        return -1;
    }

    assert(_anjay_coap_msg_is_valid(msg));
    *out_code = msg->header.code;
    return 0;
}

int _anjay_coap_stream_get_msg_type(avs_stream_abstract_t *stream_,
                                    anjay_coap_msg_type_t *out_msg_type) {
    coap_stream_t *stream = (coap_stream_t*)stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);

    const anjay_coap_msg_t *msg = get_or_receive_msg(stream);
    if (!msg) {
        return -1;
    }

    assert(_anjay_coap_msg_is_valid(msg));
    *out_msg_type = _anjay_coap_msg_header_get_type(&msg->header);
    return 0;
}

#define DEFINE_GET_OPTION_UINT(Bits) \
int _anjay_coap_stream_get_option_u##Bits (avs_stream_abstract_t *stream_, \
                                           uint16_t option_number, \
                                           uint##Bits##_t *out_fmt) { \
    coap_stream_t *stream = (coap_stream_t*)stream_; \
    assert(stream->vtable == &COAP_STREAM_VTABLE); \
    \
    const anjay_coap_msg_t *msg = get_or_receive_msg(stream); \
    if (!msg) { \
        return -1; \
    } \
    \
    assert(_anjay_coap_msg_is_valid(msg)); \
    \
    const anjay_coap_opt_t *opt; \
    if (_anjay_coap_msg_find_unique_opt(msg, option_number, &opt)) { \
        if (opt) { \
            coap_log(ERROR, "multiple instances of option %d found", \
                     option_number); \
            return -1; \
        } else { \
            coap_log(ERROR, "option %d not found", option_number); \
            return ANJAY_COAP_OPTION_MISSING; \
        } \
    } \
    \
    return _anjay_coap_opt_u##Bits##_value(opt, out_fmt); \
}

DEFINE_GET_OPTION_UINT(16) // _anjay_coap_stream_get_option_u16
DEFINE_GET_OPTION_UINT(32) // _anjay_coap_stream_get_option_u32

int _anjay_coap_stream_get_option_string_it(avs_stream_abstract_t *stream_,
                                            uint16_t option_number,
                                            anjay_coap_opt_iterator_t *it,
                                            size_t *out_bytes_read,
                                            char *buffer,
                                            size_t buffer_size) {
    coap_stream_t *stream = (coap_stream_t*)stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);

    if (!it->msg) {
        const anjay_coap_msg_t *msg = get_or_receive_msg(stream);
        if (!msg) {
            return -1;
        }
        anjay_coap_opt_iterator_t begin = _anjay_coap_opt_begin(msg);
        memcpy(it, &begin, sizeof(*it));
    } else {
        _anjay_coap_opt_next(it);
    }

    for (; !_anjay_coap_opt_end(it); _anjay_coap_opt_next(it)) {
        if (_anjay_coap_opt_number(it) == option_number) {
            return _anjay_coap_opt_string_value(it->curr_opt, out_bytes_read,
                                                buffer, buffer_size);
        }
    }

    return 1;
}

int _anjay_coap_stream_get_content_format(avs_stream_abstract_t *stream,
                                          uint16_t *out_value) {
    int result = _anjay_coap_stream_get_option_u16(
            stream, ANJAY_COAP_OPT_CONTENT_FORMAT, out_value);

    if (result == ANJAY_COAP_OPTION_MISSING) {
        *out_value = ANJAY_COAP_FORMAT_NONE;
        return 0;
    }

    return result;
}

int _anjay_coap_stream_get_request_identity(avs_stream_abstract_t *stream_,
                                            anjay_coap_msg_identity_t *out_id) {
    coap_stream_t *stream = (coap_stream_t*)stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);
    const anjay_coap_msg_identity_t *id = NULL;

    switch (stream->state) {
    case STREAM_STATE_CLIENT:
        id = _anjay_coap_client_get_request_identity(get_client(stream));
        break;
    case STREAM_STATE_SERVER:
        id = _anjay_coap_server_get_request_identity(get_server(stream));
        break;
    default:
        coap_log(ERROR, "get_request_identity called on an IDLE stream");
        return -1;
    }

    assert(id);
    *out_id = *id;
    return 0;
}

static bool is_opt_critical(uint32_t opt_number) {
    return opt_number % 2;
}

static bool is_critical_opt_valid(uint8_t msg_code, uint32_t opt_number,
            anjay_coap_stream_critical_option_validator_t fallback_validator) {
    switch (opt_number) {
    case ANJAY_COAP_OPT_BLOCK1:
        return msg_code == ANJAY_COAP_CODE_PUT
            || msg_code == ANJAY_COAP_CODE_POST;
    case ANJAY_COAP_OPT_BLOCK2:
        return msg_code == ANJAY_COAP_CODE_GET
            || msg_code == ANJAY_COAP_CODE_PUT
            || msg_code == ANJAY_COAP_CODE_POST;
    default:
        return fallback_validator(msg_code, opt_number);
    }
}

int _anjay_coap_stream_validate_critical_options(
        avs_stream_abstract_t *stream_,
        anjay_coap_stream_critical_option_validator_t validator) {
    coap_stream_t *stream = (coap_stream_t*)stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);

    const anjay_coap_msg_t *msg = get_or_receive_msg(stream);
    if (!msg) {
        return -1;
    }

    anjay_coap_opt_iterator_t it = _anjay_coap_opt_begin(msg);
    int result = 0;

    while (!_anjay_coap_opt_end(&it)) {
        if (is_opt_critical(_anjay_coap_opt_number(&it))) {
            uint32_t opt_number = _anjay_coap_opt_number(&it);

            if (!is_critical_opt_valid(msg->header.code, opt_number, validator)) {
                coap_log(DEBUG, "warning: invalid critical option in query %s: %u",
                         ANJAY_COAP_CODE_STRING(msg->header.code), opt_number);
                result = -1;
            }
        }
        _anjay_coap_opt_next(&it);
    }

    return result;
}
