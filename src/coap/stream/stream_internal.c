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

#include "stream_internal.h"

#include <avsystem/commons/stream/stream_net.h>
#include <avsystem/commons/stream_v_table.h>

#include "../coap_log.h"
#include "../content_format.h"

#include "../id_source/auto.h"

VISIBILITY_SOURCE_BEGIN

AVS_STATIC_ASSERT(offsetof(coap_client_t, common) == 0, client_common);
AVS_STATIC_ASSERT(offsetof(coap_server_t, common) == 0, server_common);

static coap_client_t *get_client(coap_stream_t *stream) {
    assert(stream->state == STREAM_STATE_CLIENT);
    return &stream->data.client;
}

static coap_server_t *get_server(coap_stream_t *stream) {
    assert(stream->state == STREAM_STATE_SERVER);
    return &stream->data.server;
}

static void reset(coap_stream_t *stream) {
    _anjay_coap_in_reset(&stream->data.common.in);
    _anjay_coap_out_reset(&stream->data.common.out);

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
    memset(((char *) &stream->data) + sizeof(coap_stream_common_t), 0,
           sizeof(stream->data) - sizeof(coap_stream_common_t));
    coap_log(TRACE, "stream: IDLE mode (reset)");
}

static bool is_reset(coap_stream_t *stream) {
    bool is_idle = stream->state == STREAM_STATE_IDLE;

    assert(!is_idle || _anjay_coap_in_is_reset(&stream->data.common.in));
    assert(!is_idle || _anjay_coap_out_is_reset(&stream->data.common.out));

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

static int get_or_receive_msg(coap_stream_t *stream,
                              const avs_coap_msg_t **out_msg) {
    int result = 0;

    switch (stream->state) {
    case STREAM_STATE_CLIENT:
        result = _anjay_coap_client_get_or_receive_msg(get_client(stream),
                                                       out_msg);
        break;
    case STREAM_STATE_IDLE:
        coap_log(TRACE, "get_or_receive_msg: idle stream, receiving");
        become_server(stream);
        // fall-through
    case STREAM_STATE_SERVER:
        result = _anjay_coap_server_get_or_receive_msg(get_server(stream),
                                                       out_msg);
        break;
    }

    if (result) {
        reset(stream);
        *out_msg = NULL;
    }

    return result;
}

static int setup_response(avs_stream_abstract_t *stream_,
                          const anjay_msg_details_t *details) {
    coap_stream_t *stream = (coap_stream_t *) stream_;

    if (stream->state != STREAM_STATE_SERVER) {
        coap_log(ERROR, "no request to respond to");
        return -1;
    }

    int result;
    if ((result = _anjay_coap_server_setup_response(get_server(stream),
                                                    details))) {
        reset(stream);
    }
    return result;
}

static const anjay_coap_stream_ext_t COAP_STREAM_EXT_VTABLE = {
    .setup_response = setup_response
};

static int coap_getsock(avs_stream_abstract_t *stream_,
                        avs_net_abstract_socket_t **out_sock) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    *out_sock = stream->data.common.socket;
    return *out_sock == NULL ? -1 : 0;
}

static int coap_setsock(avs_stream_abstract_t *stream_,
                        avs_net_abstract_socket_t *sock) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    if (!is_reset(stream)) {
        return -1;
    }

    if (stream->data.common.socket != NULL && sock != NULL) {
        AVS_UNREACHABLE("swapping socket on an not-yet-released stream");
        return -1;
    }

    stream->data.common.socket = sock;
    return 0;
}

static const avs_stream_v_table_extension_net_t NET_EXT_VTABLE = {
    coap_getsock, coap_setsock
};

static const avs_stream_v_table_extension_t COAP_STREAM_EXT[] = {
    { ANJAY_COAP_STREAM_EXTENSION, &COAP_STREAM_EXT_VTABLE },
    { AVS_STREAM_V_TABLE_EXTENSION_NET, &NET_EXT_VTABLE },
    AVS_STREAM_V_TABLE_EXTENSION_NULL
};

static int coap_write(avs_stream_abstract_t *stream_,
                      const void *data,
                      size_t *data_length) {
    coap_stream_t *stream = (coap_stream_t *) stream_;

    switch (stream->state) {
    case STREAM_STATE_CLIENT:
        return _anjay_coap_client_write(get_client(stream), stream->id_source,
                                        data, *data_length);
    case STREAM_STATE_SERVER:
        return _anjay_coap_server_write(get_server(stream), data, *data_length);
    default:
        coap_log(ERROR, "write called on an IDLE stream");
        return -1;
    }
}

static int coap_finish_message(avs_stream_abstract_t *stream_) {
    coap_stream_t *stream = (coap_stream_t *) stream_;

    switch (stream->state) {
    case STREAM_STATE_CLIENT:
        return _anjay_coap_client_finish_request(get_client(stream));

    case STREAM_STATE_SERVER:
        return _anjay_coap_server_finish_response(get_server(stream));

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
    coap_stream_t *stream = (coap_stream_t *) stream_;
    assert(stream->data.common.in.buffer);

    const avs_coap_msg_t *msg;
    int result = get_or_receive_msg(stream, &msg);
    if (result) {
        return result;
    }

    switch (stream->state) {
    case STREAM_STATE_IDLE:
        AVS_UNREACHABLE("should never happen");
        break;
    case STREAM_STATE_SERVER:
        result = _anjay_coap_server_read(get_server(stream), out_bytes_read,
                                         out_message_finished, buffer,
                                         buffer_length);
        break;
    case STREAM_STATE_CLIENT:
        result = _anjay_coap_client_read(get_client(stream), out_bytes_read,
                                         out_message_finished, buffer,
                                         buffer_length);
        break;
    }

    if (!result && *out_message_finished) {
        _anjay_coap_in_reset(&stream->data.common.in);
    }

    return result;
}

static int coap_reset(avs_stream_abstract_t *stream_) {
    reset((coap_stream_t *) stream_);
    return 0;
}

static int coap_close(avs_stream_abstract_t *stream_) {
    coap_stream_t *stream = (coap_stream_t *) stream_;

    reset(stream);

    if (stream->data.common.socket) {
        avs_net_socket_cleanup(&stream->data.common.socket);
    }

    if (stream->data.common.coap_ctx) {
        avs_coap_ctx_cleanup(&stream->data.common.coap_ctx);
    }

    stream->data.common.in.buffer = NULL;
    stream->data.common.out.buffer = NULL;

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
                              avs_coap_ctx_t *coap_ctx,
                              uint8_t *in_buffer,
                              size_t in_buffer_size,
                              uint8_t *out_buffer,
                              size_t out_buffer_size) {
    coap_stream_t *stream =
            (coap_stream_t *) avs_calloc(1, sizeof(coap_stream_t));
    if (!stream) {
        coap_log(ERROR, "Out of memory");
        return -1;
    }

    stream->vtable = &COAP_STREAM_VTABLE;
    stream->data.common.coap_ctx = coap_ctx;

    stream->state = STREAM_STATE_IDLE;

    stream->data.common.in.buffer_size = in_buffer_size;
    stream->data.common.in.buffer = in_buffer;
    stream->data.common.in.rand_seed =
            (anjay_rand_seed_t) avs_time_real_now().since_real_epoch.seconds;

    stream->data.common.out = _anjay_coap_out_init(out_buffer, out_buffer_size);

    stream->id_source = _anjay_coap_id_source_auto_new(
            (anjay_rand_seed_t) avs_time_real_now().since_real_epoch.seconds,
            8);

    if (!stream->data.common.in.buffer || !stream->data.common.out.buffer
            || !stream->id_source) {
        coap_log(ERROR, "Out of memory");
        coap_close((avs_stream_abstract_t *) stream);
        avs_free(stream);
        return -1;
    }
    reset(stream);

    *stream_ = (avs_stream_abstract_t *) stream;
    return 0;
}

int _anjay_coap_stream_get_tx_params(avs_stream_abstract_t *stream_,
                                     avs_coap_tx_params_t *out_tx_params) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);
    *out_tx_params = avs_coap_ctx_get_tx_params(stream->data.common.coap_ctx);
    return 0;
}

int _anjay_coap_stream_set_tx_params(avs_stream_abstract_t *stream_,
                                     const avs_coap_tx_params_t *tx_params) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);
    assert(avs_coap_tx_params_valid(tx_params, NULL));
    avs_coap_ctx_set_tx_params(stream->data.common.coap_ctx, tx_params);
    return 0;
}

int _anjay_coap_stream_setup_response(avs_stream_abstract_t *stream,
                                      const anjay_msg_details_t *details) {
    const anjay_coap_stream_ext_t *coap =
            (const anjay_coap_stream_ext_t *) avs_stream_v_table_find_extension(
                    stream, ANJAY_COAP_STREAM_EXTENSION);
    if (coap) {
        return coap->setup_response(stream, details);
    }
    AVS_UNREACHABLE("`coap' pointer is NULL");
    return -1;
}

int _anjay_coap_stream_setup_request(avs_stream_abstract_t *stream_,
                                     const anjay_msg_details_t *details,
                                     const avs_coap_token_t *token) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
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

    avs_coap_msg_identity_t identity =
            _anjay_coap_id_source_get(stream->id_source);
    if (token) {
        identity.token = *token;
    }

    int result;
    if ((result = _anjay_coap_client_setup_request(get_client(stream), details,
                                                   &identity))) {
        reset(stream);
    }
    return result;
}

int _anjay_coap_stream_set_error(avs_stream_abstract_t *stream_, uint8_t code) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);

    if (stream->state != STREAM_STATE_SERVER) {
        coap_log(ERROR, "set_error only makes sense on a server mode stream");
        return -1;
    }

    _anjay_coap_server_set_error(get_server(stream), code);
    return 0;
}

int _anjay_coap_stream_get_incoming_msg(avs_stream_abstract_t *stream_,
                                        const avs_coap_msg_t **out_msg) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);

    int result = get_or_receive_msg(stream, out_msg);
    if (result) {
        return result;
    }

    assert(avs_coap_msg_is_valid(*out_msg));
    return 0;
}

int _anjay_coap_stream_get_request_identity(avs_stream_abstract_t *stream_,
                                            avs_coap_msg_identity_t *out_id) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);
    const avs_coap_msg_identity_t *id = NULL;

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

void _anjay_coap_stream_set_block_request_validator(
        avs_stream_abstract_t *stream_,
        anjay_coap_block_request_validator_t *validator,
        void *validator_arg) {
    coap_stream_t *stream = (coap_stream_t *) stream_;
    assert(stream->vtable == &COAP_STREAM_VTABLE);

    _anjay_coap_server_set_block_request_relation_validator(
            get_server(stream), validator, validator_arg);
}
