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

#define AVS_COAP_POISON_H // disable libc poisoning
#include <avs_coap_init.h>

#include <avsystem/coap/coap.h>

#include <avsystem/commons/avs_log.h>
#include <avsystem/commons/avs_prng.h>
#include <avsystem/commons/avs_socket.h>
#include <avsystem/commons/avs_socket_v_table.h>
#include <avsystem/commons/avs_utils.h>

#include <inttypes.h>
#include <stdio.h>

#define MODULE_NAME fuzz
#include <avs_coap_x_log_config.h>

#include "avs_coap_ctx.h"

static uint16_t g_mtu = 1500;
static avs_sched_t *g_sched = NULL;

static avs_error_t unexpected() {
    abort();
    return avs_errno(AVS_UNKNOWN_ERROR);
}

static const void *unexpected_system_socket() {
    abort();
    return NULL;
}

static bool read_flag(void) {
    uint8_t value = false;
    size_t bytes_read = fread(&value, sizeof(value), 1, stdin);
    (void) bytes_read;
    LOG(DEBUG, "read_flag: %02x", (unsigned) value);
    return value;
}

static void dump_buffer(const char *label, const void *buffer, size_t size) {
    LOG(DEBUG, "%s", label);
    if (avs_log_should_log__(AVS_LOG_DEBUG, "fuzz")) {
#define CHUNK_SIZE 256
        for (size_t offset = 0; offset < size; offset += CHUNK_SIZE) {
            char buf[CHUNK_SIZE * 2 + 1];
            avs_hexlify(buf, sizeof(buf), NULL, (const char *) buffer + offset,
                        size - offset);
            fprintf(stderr, "%s", buf);
        }
        fprintf(stderr, "\n");
    }
}

static avs_coap_options_t read_options__(char buf[static 65535]) {
    avs_coap_options_t opts = { 0 };

    uint16_t options_size;
    uint16_t options_capacity;
    if (fread(&options_size, sizeof(options_size), 1, stdin) != 1
            || fread(&options_capacity, sizeof(options_capacity), 1, stdin)
                           != 1) {
        LOG(DEBUG, "read_options: EOF");
        return (avs_coap_options_t) { 0 };
    }

    opts.size = options_size;
    opts.capacity = options_capacity;
    opts.begin = buf;
    if (fread(buf, 1, opts.size, stdin) != opts.size) {
        LOG(DEBUG, "read options: EOF");
        return (avs_coap_options_t) { 0 };
    }

    dump_buffer("opts", opts.begin, opts.size);
    return opts;
}

#define read_options() read_options__(&(char[65535]){ 0 }[0])

static avs_error_t get_opt(avs_net_socket_t *socket,
                           avs_net_socket_opt_key_t option_key,
                           avs_net_socket_opt_value_t *out_option_value) {
    (void) socket;

    if (option_key == AVS_NET_SOCKET_OPT_INNER_MTU) {
        out_option_value->mtu = g_mtu;
        return AVS_OK;
    }
    return avs_errno(AVS_ENOTSUP);
}

static uint8_t get_token_length(uint8_t first_byte) {
    return first_byte & 0x0F;
}

uint8_t g_last_send[12];

static avs_error_t mock_recv(avs_net_socket_t *socket,
                             size_t *out_bytes_received,
                             void *buffer,
                             size_t buffer_length) {
    (void) socket;

    uint16_t msg_size;
    if (fread(&msg_size, sizeof(msg_size), 1, stdin) != 1
            || msg_size == UINT16_MAX) {
        LOG(DEBUG, "mock_recv: fail (size == %" PRIu64 ")",
            (uint64_t) msg_size);
        return avs_errno(AVS_EIO);
    }

    char *tmp_buf = (char *) malloc(msg_size);
    if (!tmp_buf) {
        return avs_errno(AVS_ENOMEM);
    }

    *out_bytes_received = fread(tmp_buf, 1, msg_size, stdin);
    *out_bytes_received = AVS_MIN(*out_bytes_received, buffer_length);
    memcpy(buffer, tmp_buf, *out_bytes_received);

    const size_t token_offset = 4;
    uint8_t new_token_length = get_token_length(((uint8_t *) buffer)[0]);
    size_t options_offset = token_offset + new_token_length;
    if (read_flag() && new_token_length <= 8
            && *out_bytes_received >= options_offset) {
        size_t options_and_payload_size = *out_bytes_received - options_offset;
        uint8_t last_toklen = get_token_length(g_last_send[0]);
        if (options_and_payload_size + last_toklen + token_offset
                <= buffer_length) {
            // copy options and payload
            memcpy(buffer + token_offset + last_toklen, buffer + options_offset,
                   options_and_payload_size);
            LOG(DEBUG, "mock_recv: echo token (%u B)", (unsigned) last_toklen);
            memcpy(buffer + token_offset,
                   &g_last_send[token_offset],
                   last_toklen);
            ((uint8_t *) buffer)[0] &= 0xF0;
            ((uint8_t *) buffer)[0] |= (uint8_t) last_toklen;
            LOG(DEBUG, "mock_recv: echo ID");
            memcpy(&(((char *) buffer)[2]), &g_last_send[2], 2);
        }
    }

    dump_buffer("recv", buffer, *out_bytes_received);
    LOG(DEBUG, "mock_recv: OK, %" PRIu64 " B received, %" PRIu64 " B reported",
        (uint64_t) msg_size, (uint64_t) *out_bytes_received);

    free(tmp_buf);
    return AVS_OK;
}

static avs_error_t
mock_send(avs_net_socket_t *socket, const void *buffer, size_t size) {
    (void) socket;
    (void) buffer;

    memcpy(g_last_send, buffer, AVS_MIN(size, sizeof(g_last_send)));

    dump_buffer("send", buffer, size);
    if (read_flag()) {
        LOG(DEBUG, "mock_send: fail");
        return avs_errno(AVS_EIO);
    }
    LOG(DEBUG, "mock_send: OK");
    return AVS_OK;
}

static avs_error_t empty_string_getter(avs_net_socket_t *socket,
                                       char *out_buffer,
                                       size_t out_buffer_size) {
    (void) socket;
    (void) out_buffer_size;

    assert(out_buffer_size > 0);
    *out_buffer = '\0';
    return AVS_OK;
}

static const avs_net_socket_v_table_t MOCK_SOCKET_VTABLE = {
    .receive = mock_recv,
    .accept = (avs_net_socket_accept_t) unexpected,
    .bind = (avs_net_socket_bind_t) unexpected,
    .cleanup = (avs_net_socket_cleanup_t) unexpected,
    .close = (avs_net_socket_close_t) unexpected,
    .connect = (avs_net_socket_connect_t) unexpected,
    .decorate = (avs_net_socket_decorate_t) unexpected,
    .get_interface_name = (avs_net_socket_get_interface_t) unexpected,
    .get_local_port = (avs_net_socket_get_local_port_t) unexpected,
    .get_opt = get_opt,
    .get_remote_host = empty_string_getter,
    .get_remote_port = empty_string_getter,
    .get_system_socket = unexpected_system_socket,
    .receive_from = (avs_net_socket_receive_from_t) unexpected,
    .send = mock_send,
    .send_to = (avs_net_socket_send_to_t) unexpected,
    .set_opt = (avs_net_socket_set_opt_t) unexpected,
    .shutdown = (avs_net_socket_shutdown_t) unexpected,
};

const avs_net_socket_v_table_t *g_vtable_ptr = &MOCK_SOCKET_VTABLE;
avs_net_socket_t *const g_mocksock = (avs_net_socket_t *) &g_vtable_ptr;

static void do_stuff(avs_coap_ctx_t *ctx);

static int payload_writer(size_t payload_offset,
                          void *payload_buf,
                          size_t payload_buf_size,
                          size_t *out_payload_chunk_size,
                          void *arg) {
    (void) payload_offset;

    do_stuff((avs_coap_ctx_t *) arg);

    if (read_flag()) {
        LOG(DEBUG, "payload_writer: fail");
        return -1;
    }

    uint16_t payload_bytes;
    if (fread(&payload_bytes, sizeof(payload_bytes), 1, stdin) != 1) {
        payload_bytes = 0;
    }
    *out_payload_chunk_size = AVS_MIN(payload_bytes, payload_buf_size);
    memset(payload_buf, 0, *out_payload_chunk_size);

    LOG(DEBUG, "payload_writer read %" PRIu64 " / %" PRIu64,
        (uint64_t) *out_payload_chunk_size, (uint64_t) payload_buf_size);
    dump_buffer("payload_writer", payload_buf, *out_payload_chunk_size);
    return 0;
}

static void response_handler(avs_coap_ctx_t *ctx,
                             avs_coap_exchange_id_t exchange_id,
                             avs_coap_client_request_state_t state,
                             const avs_coap_client_async_response_t *response,
                             avs_error_t err,
                             void *arg) {
    (void) exchange_id;
    (void) state;
    (void) response;
    (void) err;
    (void) arg;

    LOG(DEBUG, "response_handler");
    if (response) {
        dump_buffer("response payload", response->payload,
                    response->payload_size);
    }

    do_stuff(ctx);
}

static int handle_request(avs_coap_request_ctx_t *ctx,
                          avs_coap_exchange_id_t request_id,
                          avs_coap_server_request_state_t state,
                          const avs_coap_server_async_request_t *request,
                          const avs_coap_observe_id_t *observe_id,
                          void *arg) {
    avs_coap_ctx_t *coap_ctx = (avs_coap_ctx_t *) arg;

    (void) request_id;
    (void) state;
    (void) request;
    (void) observe_id;

    do_stuff(coap_ctx);

    if (read_flag()) {
        int result = 0;
        if (fread(&result, sizeof(result), 1, stdin) != 1) {
            LOG(DEBUG, "handle_request: EOF");
            return 0;
        }
        LOG(DEBUG, "handle_request: early return, result = %d", result);
        return result;
    }

    avs_coap_response_header_t response;
    if (fread(&response.code, sizeof(response.code), 1, stdin) != 1) {
        LOG(DEBUG, "handle_request: EOF");
        return 0;
    }
    LOG(DEBUG, "handle_request: response code = %u", response.code);
    response.options = read_options();

    avs_error_t err = AVS_OK;
    if (ctx) {
        err = avs_coap_server_setup_async_response(
                ctx, &response, read_flag() ? payload_writer : NULL, coap_ctx);
        LOG(DEBUG, "handle_request: avs_coap_server_setup_async_response: %s",
            AVS_COAP_STRERROR(err));
    }

    do_stuff(coap_ctx);

    int result = 0;
    if (fread(&result, sizeof(result), 1, stdin) != 1) {
        LOG(DEBUG, "handle_request: EOF");
        return 0;
    }
    LOG(DEBUG, "handle_request: result = %d", result);
    return result;
}

static int handle_new_request(avs_coap_server_ctx_t *ctx,
                              const avs_coap_request_header_t *request,
                              void *arg) {
    avs_coap_ctx_t *coap_ctx = (avs_coap_ctx_t *) arg;

    (void) request;

    do_stuff(coap_ctx);

    if (read_flag()) {
        int result = 0;
        if (fread(&result, sizeof(result), 1, stdin) != 1) {
            LOG(DEBUG, "handle_new_request: EOF");
            return 0;
        }
        LOG(DEBUG, "handle_new_request: early return, result = %d", result);
        return result;
    }

    avs_coap_exchange_id_t id =
            avs_coap_server_accept_async_request(ctx, handle_request, coap_ctx);
    LOG(DEBUG, "handle_new_request: accept ID = %" PRIu64, id.value);

    do_stuff(coap_ctx);

    int result = 0;
    if (fread(&result, sizeof(result), 1, stdin) != 1) {
        LOG(DEBUG, "handle_new_request: EOF");
        return 0;
    }
    LOG(DEBUG, "handle_new_request: result = %d", result);
    return result;
}

static void do_stuff_unconditionally(avs_coap_ctx_t *ctx) {
    enum {
        FLAG_PASS_ID = (1 << 0),
        FLAG_PASS_WRITER = (1 << 1),
        FLAG_PASS_HANDLER = (1 << 2),
    };

    enum {
        OP_NOOP,
        OP_SEND_ASYNC_REQUEST,
        OP_EXCHANGE_CANCEL,
        OP_HANDLE_INCOMING_PACKET,
        OP_SCHED_RUN,
    };

    uint8_t operation;
    if (fread(&operation, sizeof(operation), 1, stdin) != 1) {
        LOG(DEBUG, "do_stuff: EOF");
        return;
    }
    LOG(DEBUG, "do_stuff: %u", operation);

    switch (operation) {
    case OP_NOOP:
        LOG(DEBUG, "noop");
        return;
    case OP_SEND_ASYNC_REQUEST: {
        uint8_t flags;
        if (fread(&flags, sizeof(flags), 1, stdin) != 1) {
            LOG(DEBUG, "read flags: EOF");
            return;
        }

        bool pass_id = (flags & FLAG_PASS_ID);
        bool pass_writer = (flags & FLAG_PASS_WRITER);
        bool pass_handler = (flags & FLAG_PASS_HANDLER);

        avs_coap_exchange_id_t id;
        avs_coap_request_header_t req = { 0 };
        if (fread(&req.code, sizeof(req.code), 1, stdin) != 1) {
            LOG(DEBUG, "read details: EOF");
            return;
        }

        req.options = read_options();
        LOG(DEBUG, "avs_coap_client_send_async_request");
        avs_coap_client_send_async_request(ctx,
                                           pass_id ? &id : NULL,
                                           &req,
                                           pass_writer ? payload_writer : NULL,
                                           ctx,
                                           pass_handler ? response_handler
                                                        : NULL,
                                           ctx);

        break;
    }
    case OP_EXCHANGE_CANCEL: {
        avs_coap_exchange_id_t id;
        if (fread(&id, sizeof(id), 1, stdin) != 1) {
            LOG(DEBUG, "read ID: EOF");
            return;
        }
        LOG(DEBUG, "avs_coap_exchange_cancel %" PRIu64, id.value);
        avs_coap_exchange_cancel(ctx, id);
        break;
    }
    case OP_HANDLE_INCOMING_PACKET: {
        LOG(DEBUG, "avs_coap_async_handle_incoming_packet");
        avs_coap_async_handle_incoming_packet(ctx, handle_new_request, ctx);
        break;
    }
    case OP_SCHED_RUN: {
        LOG(DEBUG, "avs_sched_run");
        avs_sched_run(g_sched);
        break;
    }
    default:
        break;
    }
}

static void do_stuff(avs_coap_ctx_t *ctx) {
    static const size_t RECURSION_LIMIT = 20;
    static size_t recursion_depth = 0;

    if (recursion_depth >= RECURSION_LIMIT) {
        LOG(DEBUG, "do_stuff: recursion limit reached, returning");
        return;
    }

    ++recursion_depth;
    do_stuff_unconditionally(ctx);
    --recursion_depth;
}

int main() {
    if (getenv("VERBOSE")) {
        avs_log_set_default_level(AVS_LOG_TRACE);
    }

    avs_shared_buffer_t *in_buffer = NULL;
    avs_shared_buffer_t *out_buffer = NULL;
    avs_crypto_prng_ctx_t *prng_ctx = NULL;
    avs_coap_ctx_t *ctx = NULL;
    avs_coap_udp_response_cache_t *cache = NULL;

    uint16_t in_buf_size;
    uint16_t out_buf_size;
    uint16_t cache_size = 0;
    avs_coap_udp_tx_params_t tx_params = AVS_COAP_DEFAULT_UDP_TX_PARAMS;

    if (!(g_sched = avs_sched_new("sched", NULL))
            || fread(&in_buf_size, sizeof(in_buf_size), 1, stdin) != 1
            || fread(&out_buf_size, sizeof(out_buf_size), 1, stdin) != 1
            || (read_flag()
                && fread(&tx_params, sizeof(tx_params), 1, stdin) != 1)
            || (read_flag()
                && fread(&cache_size, sizeof(cache_size), 1, stdin) != 1)
            || (read_flag() && fread(&g_mtu, sizeof(g_mtu), 1, stdin) != 1)
            || !(in_buffer = avs_shared_buffer_new(in_buf_size))
            || !(out_buffer = avs_shared_buffer_new(out_buf_size))
            || !(prng_ctx = avs_crypto_prng_new(NULL, NULL))
            || (cache_size
                && !(cache = avs_coap_udp_response_cache_create(cache_size)))
            || !(ctx = avs_coap_udp_ctx_create(g_sched, &tx_params, in_buffer,
                                               out_buffer, cache, prng_ctx))
            || avs_is_err(avs_coap_ctx_set_socket(ctx, g_mocksock))) {
        goto exit;
    }

    g_mtu %= (1 << 16);

    while (!feof(stdin)) {
        do_stuff(ctx);
    }

exit:
    avs_coap_ctx_cleanup(&ctx);
    avs_sched_cleanup(&g_sched);
    avs_free(in_buffer);
    avs_free(out_buffer);
    avs_crypto_prng_free(&prng_ctx);
}
