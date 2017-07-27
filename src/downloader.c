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

#include <inttypes.h>
#include <strings.h>

#include <avsystem/commons/stream/net.h>
#include <anjay/download.h>

#include "coap/block_utils.h"
#include "coap/id_source/id_source.h"
#include "coap/msg_builder.h"
#include "coap/msg_opt.h"
#include "coap/socket.h"
#include "coap/stream.h"
#include "coap/tx_params.h"
#include "downloader.h"
#include "utils.h"
#include "anjay.h"

#define dl_log(...) avs_log(downloader, __VA_ARGS__)

#define INVALID_DOWNLOAD_ID ((uintptr_t) NULL)

VISIBILITY_SOURCE_BEGIN

struct anjay_download_ctx {
    uintptr_t id;

    anjay_url_t uri;
    size_t bytes_downloaded;
    size_t block_size;
    anjay_etag_t etag;

    anjay_download_next_block_handler_t *on_next_block;
    anjay_download_finished_handler_t *on_download_finished;
    void *user_data;

    avs_net_abstract_socket_t *socket;
    anjay_coap_msg_identity_t last_req_id;

    /*
     * After calling @ref _anjay_downloader_download:
     *     handle to a job that sends the initial request.
     * During the download (after sending the initial request):
     *     handle to retransmission job.
     * After receiving a separate ACK:
     *     handle to a job aborting the transfer if no Separate Response was
     *     received.
     */
    anjay_sched_handle_t sched_job;
};

static anjay_t *get_anjay(anjay_downloader_t *dl) {
    return AVS_CONTAINER_OF(dl, anjay_t, downloader);
}

int _anjay_downloader_init(anjay_downloader_t *dl,
                           anjay_t *anjay,
                           coap_id_source_t **id_source_move) {
    assert(anjay);
    assert(get_anjay(dl) == anjay);
    assert(get_anjay(dl)->sched);
    assert(get_anjay(dl)->coap_socket);

    if (!anjay || anjay != get_anjay(dl) || !anjay->sched
            || !anjay->coap_socket) {
        dl_log(ERROR, "invalid anjay pointer passed");
        return -1;
    }

    *dl = (anjay_downloader_t) {
        .id_source = *id_source_move,
        .next_id = 1,
        .downloads = NULL,
    };

    *id_source_move = NULL;
    return 0;
}

static void cleanup_transfer(anjay_downloader_t *dl,
                             AVS_LIST(anjay_download_ctx_t) *ctx) {
    assert(ctx);
    assert(*ctx);

    if ((*ctx)->sched_job) {
        _anjay_sched_del(get_anjay(dl)->sched, &(*ctx)->sched_job);
    }
    _anjay_url_cleanup(&(*ctx)->uri);
#ifndef ANJAY_TEST
    avs_net_socket_cleanup(&(*ctx)->socket);
#endif // ANJAY_TEST
    AVS_LIST_DELETE(ctx);
}

static void abort_transfer(anjay_downloader_t *dl,
                           AVS_LIST(anjay_download_ctx_t) *ctx,
                           int result) {
    assert(ctx);
    assert(*ctx);

    dl_log(TRACE, "aborting download id = %" PRIuPTR ", result = %d",
           (*ctx)->id, result);

    (*ctx)->on_download_finished(get_anjay(dl), result, (*ctx)->user_data);

    cleanup_transfer(dl, ctx);
}

void _anjay_downloader_cleanup(anjay_downloader_t *dl) {
    assert(dl);
    while (dl->downloads) {
        abort_transfer(dl, &dl->downloads, ANJAY_DOWNLOAD_ERR_ABORTED);
    }

    _anjay_coap_id_source_release(&dl->id_source);
}

static AVS_LIST(anjay_download_ctx_t) *
find_ctx_ptr_by_socket(anjay_downloader_t *dl,
                       avs_net_abstract_socket_t *socket) {
    AVS_LIST(anjay_download_ctx_t) *ctx;
    AVS_LIST_FOREACH_PTR(ctx, &dl->downloads) {
        if ((*ctx)->socket == socket) {
            return ctx;
        }
    }

    return NULL;
}

int _anjay_downloader_get_sockets(
        anjay_downloader_t *dl,
        AVS_LIST(avs_net_abstract_socket_t *const) *out_socks) {
    AVS_LIST(avs_net_abstract_socket_t *const) sockets = NULL;
    AVS_LIST(anjay_download_ctx_t) dl_ctx;

    AVS_LIST_FOREACH(dl_ctx, dl->downloads) {
        AVS_LIST(avs_net_abstract_socket_t *) elem =
                AVS_LIST_NEW_ELEMENT(avs_net_abstract_socket_t *);
        if (!elem) {
            AVS_LIST_CLEAR(&sockets);
            return -1;
        }

        *elem = dl_ctx->socket;
        AVS_LIST_INSERT(&sockets, elem);
    }

    AVS_LIST_INSERT(out_socks, sockets);
    return 0;
}

static int fill_request_info(anjay_coap_msg_info_t *req_info,
                             const anjay_coap_msg_identity_t *id,
                             const anjay_download_ctx_t *ctx) {
    req_info->type = ANJAY_COAP_MSG_CONFIRMABLE;
    req_info->code = ANJAY_COAP_CODE_GET;
    req_info->identity = *id;

    AVS_LIST(anjay_string_t) elem;
    AVS_LIST_FOREACH(elem, ctx->uri.uri_path) {
        if (_anjay_coap_msg_info_opt_string(req_info, ANJAY_COAP_OPT_URI_PATH,
                                            elem->c_str)) {
            return -1;
        }
    }
    AVS_LIST_FOREACH(elem, ctx->uri.uri_query) {
        if (_anjay_coap_msg_info_opt_string(req_info, ANJAY_COAP_OPT_URI_QUERY,
                                            elem->c_str)) {
            return -1;
        }
    }

    coap_block_info_t block2 = {
        .type = COAP_BLOCK2,
        .valid = true,
        .seq_num = (uint32_t)(ctx->bytes_downloaded / ctx->block_size),
        .size = (uint16_t)ctx->block_size,
        .has_more = false
    };
    if (_anjay_coap_msg_info_opt_block(req_info, &block2)) {
        return -1;
    }

    return 0;
}

static int request_block(anjay_downloader_t *dl,
                         anjay_download_ctx_t *ctx);

static AVS_LIST(anjay_download_ctx_t) *
find_ctx_ptr_by_id(anjay_downloader_t *dl,
                   uintptr_t id) {
    AVS_LIST(anjay_download_ctx_t) *ctx;
    AVS_LIST_FOREACH_PTR(ctx, &dl->downloads) {
        if ((*ctx)->id == id) {
            return ctx;
        }
    }

    return NULL;
}

static int request_block_job(anjay_t *anjay,
                             void *id_) {
    uintptr_t id = (uintptr_t)id_;

    AVS_LIST(anjay_download_ctx_t) *ctx =
            find_ctx_ptr_by_id(&anjay->downloader, id);
    if (!ctx) {
        dl_log(DEBUG, "download id = %" PRIuPTR " not found (expired?)", id);
        return 0;
    }

    request_block(&anjay->downloader, *ctx);

    // return non-zero to ensure job retries
    return -1;
}

static int schedule_retransmission(anjay_downloader_t *dl,
                                   anjay_download_ctx_t *ctx) {
    const anjay_coap_tx_params_t *tx_params = &get_anjay(dl)->udp_tx_params;

    coap_retry_state_t retry_state = { 0,  0 };

    // first retry
    _anjay_coap_update_retry_state(&retry_state, tx_params, &dl->rand_seed);
    struct timespec delay = {
        .tv_sec = retry_state.recv_timeout_ms / 1000,
        .tv_nsec = 1000 * 1000 * (retry_state.recv_timeout_ms % 1000)
    };

    // second retry
    _anjay_coap_update_retry_state(&retry_state, tx_params, &dl->rand_seed);
    anjay_sched_retryable_backoff_t backoff = {
        .delay = {
            .tv_sec = retry_state.recv_timeout_ms / 1000,
            .tv_nsec = 1000 * 1000 * (retry_state.recv_timeout_ms % 1000)
        },
        .max_delay = _anjay_coap_max_transmit_span(tx_params)
    };

    _anjay_sched_del(get_anjay(dl)->sched, &ctx->sched_job);
    return _anjay_sched_retryable(get_anjay(dl)->sched, &ctx->sched_job,
                                  delay, backoff, request_block_job,
                                  (void*)ctx->id);
}

static int request_block(anjay_downloader_t *dl,
                         anjay_download_ctx_t *ctx) {
    anjay_coap_msg_info_t info = _anjay_coap_msg_info_init();
    int result = -1;

    if (fill_request_info(&info, &ctx->last_req_id, ctx)) {
        goto finish;
    }

    const size_t required_storage_size =
            _anjay_coap_msg_info_get_packet_storage_size(&info, 0);
    if (required_storage_size > get_anjay(dl)->out_buffer_size) {
        dl_log(ERROR, "CoAP output buffer too small to hold download request "
                      "(at least %zu bytes is needed)",
               required_storage_size);
        goto finish;
    }
    anjay_coap_msg_builder_t builder;
    _anjay_coap_msg_builder_init(&builder, _anjay_coap_ensure_aligned_buffer(
                                                   get_anjay(dl)->out_buffer),
                                 get_anjay(dl)->out_buffer_size, &info);

    const anjay_coap_msg_t *msg = _anjay_coap_msg_builder_get_msg(&builder);

    _anjay_coap_socket_set_backend(get_anjay(dl)->coap_socket, ctx->socket);
    result = _anjay_coap_socket_send(get_anjay(dl)->coap_socket, msg);
    _anjay_coap_socket_set_backend(get_anjay(dl)->coap_socket, NULL);

    if (result) {
        dl_log(ERROR, "could not send request: %d", result);
    }

finish:
    _anjay_coap_msg_info_reset(&info);
    return result;
}

static int request_next_block(anjay_downloader_t *dl,
                              AVS_LIST(anjay_download_ctx_t) *ctx) {
    (*ctx)->last_req_id = _anjay_coap_id_source_get(dl->id_source);

    if (request_block(dl, *ctx)
            || schedule_retransmission(dl, *ctx)) {
        dl_log(WARNING, "could not request block starting at %zu for download "
               "id = %" PRIuPTR, (*ctx)->bytes_downloaded, (*ctx)->id);
        abort_transfer(dl, ctx, ANJAY_DOWNLOAD_ERR_FAILED);
        return -1;
    }

    return 0;
}

static int request_next_block_job(anjay_t *anjay,
                                  void *id_) {
    uintptr_t id = (uintptr_t)id_;
    AVS_LIST(anjay_download_ctx_t) *ctx =
            find_ctx_ptr_by_id(&anjay->downloader, id);
    if (!ctx) {
        dl_log(DEBUG, "download id = %" PRIuPTR "expired", id);
        return 0;
    }

    return request_next_block(&anjay->downloader, ctx);
}

static inline const char *etag_to_string(char *buf,
                                         size_t buf_size,
                                         const anjay_etag_t *etag) {
    assert(buf_size >= sizeof(etag->value) * 3 + 1
            && "buffer too small to hold ETag");

    for (size_t i = 0; i < etag->size; ++i) {
        snprintf(&buf[i * 3], buf_size - i * 3, "%02x ", etag->value[i]);
    }

    return buf;
}

#define ETAG_STR(EtagPtr) etag_to_string(&(char[32]){0}[0], 32, (EtagPtr))

static int read_etag(const anjay_coap_msg_t *msg,
                     anjay_etag_t *out_etag) {
    const anjay_coap_opt_t *etag_opt = NULL;
    int result = _anjay_coap_msg_find_unique_opt(msg, ANJAY_COAP_OPT_ETAG,
                                                 &etag_opt);
    if (!etag_opt) {
        dl_log(TRACE, "no ETag option");
        out_etag->size = 0;
        return 0;
    }

    if (etag_opt && result) {
        dl_log(DEBUG, "multiple ETag options found");
        return -1;
    }

    uint32_t etag_size = _anjay_coap_opt_content_length(etag_opt);
    if (etag_size > sizeof(out_etag->value)) {
        dl_log(DEBUG, "invalid ETag option size");
        return -1;
    }

    out_etag->size = (uint8_t)etag_size;
    memcpy(out_etag->value, _anjay_coap_opt_value(etag_opt), out_etag->size);

    dl_log(TRACE, "ETag: %s", ETAG_STR(out_etag));
    return 0;
}

static inline bool etag_matches(const anjay_etag_t *a,
                                const anjay_etag_t *b) {
    return a->size == b->size && !memcmp(a->value, b->value, a->size);
}

static int parse_response(const anjay_coap_msg_t *msg,
                          anjay_download_ctx_t *ctx,
                          coap_block_info_t *out_block2,
                          anjay_etag_t *out_etag) {
    if (read_etag(msg, out_etag)) {
        return -1;
    }

    int result = _anjay_coap_get_block_info(msg, COAP_BLOCK2, out_block2);
    if (result) {
        dl_log(DEBUG, "malformed response");
        return -1;
    }

    if (!out_block2->valid) {
        dl_log(DEBUG, "BLOCK2 option missing");
        return -1;
    }

    if (out_block2->has_more
            && out_block2->size != _anjay_coap_msg_payload_length(msg)) {
        dl_log(DEBUG, "malformed response: mismatched size of intermediate "
               "packet");
        return -1;
    }


    const size_t requested_seq_num = ctx->bytes_downloaded / ctx->block_size;
    const size_t expected_offset = requested_seq_num * ctx->block_size;
    const size_t obtained_offset = out_block2->seq_num * out_block2->size;
    if (expected_offset != obtained_offset) {
        dl_log(DEBUG,
               "expected to get data from offset %zu but got %zu instead",
               expected_offset, obtained_offset);
        return -1;
    }

    if (out_block2->size > ctx->block_size) {
        dl_log(DEBUG, "block size renegotiation failed: requested %zu, got %zu",
               ctx->block_size, (size_t)out_block2->size);
        return -1;
    } else if (out_block2->size < ctx->block_size) {
        // Allow late block size renegotiation, as we may be in the middle of
        // a download resumption, in which case we have no idea what block size
        // is appropriate. If it is not the case, and the server decided to send
        // us smaller blocks instead, it won't hurt us to get them anyway.
        dl_log(DEBUG, "block size renegotiated: %zu -> %zu",
               (size_t) ctx->block_size, (size_t) out_block2->size);
        ctx->block_size = out_block2->size;
    }

    return 0;
}

static void handle_response(const anjay_coap_msg_t *msg,
                            anjay_downloader_t *dl,
                            AVS_LIST(anjay_download_ctx_t) *ctx) {
    if (msg->header.code != ANJAY_COAP_CODE_CONTENT) {
        dl_log(DEBUG, "server responded with %s (expected %s)",
               ANJAY_COAP_CODE_STRING(msg->header.code),
               ANJAY_COAP_CODE_STRING(ANJAY_COAP_CODE_CONTENT));
        abort_transfer(dl, ctx, -msg->header.code);
        return;
    }

    coap_block_info_t block2;
    anjay_etag_t etag;
    if (parse_response(msg, *ctx, &block2, &etag)) {
        abort_transfer(dl, ctx, ANJAY_DOWNLOAD_ERR_FAILED);
        return;
    }

    if ((*ctx)->bytes_downloaded == 0) {
        assert((*ctx)->etag.size == 0 && "overwriting ETag!? we're supposed "
               "to be handling the first packet!");
        (*ctx)->etag = etag;
    } else if (!etag_matches(&etag, &(*ctx)->etag)) {
        dl_log(DEBUG, "remote resource expired, aborting download");
        abort_transfer(dl, ctx, ANJAY_DOWNLOAD_ERR_EXPIRED);
        return;
    }

    const void *payload = _anjay_coap_msg_payload(msg);
    size_t payload_size = _anjay_coap_msg_payload_length(msg);

    // Resumption from a non-multiple block-size
    size_t offset = (*ctx)->bytes_downloaded % (*ctx)->block_size;
    if (offset) {
        payload = (const char *) payload + offset;
        payload_size -= offset;
    }

    if ((*ctx)->on_next_block(get_anjay(dl), (const uint8_t *) payload,
                              payload_size, &etag, (*ctx)->user_data)) {
        abort_transfer(dl, ctx, ANJAY_DOWNLOAD_ERR_FAILED);
        return;
    }

    (*ctx)->bytes_downloaded += payload_size;
    if (!block2.has_more) {
        dl_log(INFO, "transfer id = %" PRIuPTR " finished", (*ctx)->id);
        abort_transfer(dl, ctx, 0);
    } else if (!request_next_block(dl, ctx)) {
        dl_log(TRACE, "transfer id = %" PRIuPTR ": %zu B downloaded",
               (*ctx)->id, (*ctx)->bytes_downloaded);
    }
}

static int abort_transfer_job(anjay_t *anjay,
                              void *ctx_) {
    AVS_LIST(anjay_download_ctx_t) ctx = (AVS_LIST(anjay_download_ctx_t))ctx_;
    AVS_LIST(anjay_download_ctx_t) *ctx_ptr =
            AVS_LIST_FIND_PTR(&anjay->downloader.downloads, ctx);

    if (!ctx_ptr) {
        anjay_log(WARNING, "transfer already aborted");
    } else {
        anjay_log(WARNING, "aborting download: response not received");
        abort_transfer(&anjay->downloader, ctx_ptr, ANJAY_DOWNLOAD_ERR_FAILED);
    }

    return 0;
}

static void handle_message(anjay_downloader_t *dl,
                           AVS_LIST(anjay_download_ctx_t) *ctx) {
    assert(ctx);
    assert(*ctx);

    anjay_coap_msg_t *msg =
            (anjay_coap_msg_t *) _anjay_coap_ensure_aligned_buffer(
                    get_anjay(dl)->in_buffer);

    _anjay_coap_socket_set_backend(get_anjay(dl)->coap_socket, (*ctx)->socket);
    int result = _anjay_coap_socket_recv(get_anjay(dl)->coap_socket, msg,
                                         get_anjay(dl)->in_buffer_size);
    _anjay_coap_socket_set_backend(get_anjay(dl)->coap_socket, NULL);

    if (result) {
        dl_log(DEBUG, "recv result: %d", result);
        return;
    }

    bool msg_id_must_match = true;
    anjay_coap_msg_type_t type = _anjay_coap_msg_header_get_type(&msg->header);
    switch (type) {
    case ANJAY_COAP_MSG_RESET:
    case ANJAY_COAP_MSG_ACKNOWLEDGEMENT:
        break;
    case ANJAY_COAP_MSG_CONFIRMABLE:
        msg_id_must_match = false; // Separate Response
        break;
    case ANJAY_COAP_MSG_NON_CONFIRMABLE:
        dl_log(DEBUG, "unexpected msg type: %d, ignoring", (int)type);
        return;
    }

    if (!_anjay_coap_msg_token_matches(msg, &(*ctx)->last_req_id)) {
        dl_log(DEBUG, "token mismatch, ignoring");
        return;
    }

    if (msg_id_must_match) {
        if (_anjay_coap_msg_get_id(msg) != (*ctx)->last_req_id.msg_id) {
            dl_log(DEBUG, "msg id mismatch (got %u, expected %u), ignoring",
                   _anjay_coap_msg_get_id(msg), (*ctx)->last_req_id.msg_id);
            return;
        } else if (type == ANJAY_COAP_MSG_RESET) {
            dl_log(DEBUG, "Reset response, aborting transfer");
            abort_transfer(dl, ctx, ANJAY_DOWNLOAD_ERR_FAILED);
            return;
        } else if (type == ANJAY_COAP_MSG_ACKNOWLEDGEMENT
                   && msg->header.code == ANJAY_COAP_CODE_EMPTY) {
            struct timespec abort_delay =
                _anjay_coap_exchange_lifetime(&get_anjay(dl)->udp_tx_params);
            dl_log(DEBUG, "Separate ACK received, waiting %ld.%09ld for "
                   "response", (long)abort_delay.tv_sec, abort_delay.tv_nsec);

            _anjay_sched_del(get_anjay(dl)->sched, &(*ctx)->sched_job);
            _anjay_sched(get_anjay(dl)->sched, &(*ctx)->sched_job,
                         abort_delay, abort_transfer_job, *ctx);
            return;
        }
    } else {
        dl_log(TRACE, "Separate Response received");
        _anjay_coap_socket_set_backend(get_anjay(dl)->coap_socket, (*ctx)->socket);
        _anjay_coap_send_empty(get_anjay(dl)->coap_socket,
                               ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                               _anjay_coap_msg_get_id(msg));
        _anjay_coap_socket_set_backend(get_anjay(dl)->coap_socket, NULL);
    }

    handle_response(msg, dl, ctx);
}

int _anjay_downloader_handle_packet(anjay_downloader_t *dl,
                                    avs_net_abstract_socket_t *socket) {
    assert(&get_anjay(dl)->downloader == dl);

    AVS_LIST(anjay_download_ctx_t) *ctx =
            find_ctx_ptr_by_socket(dl, socket);
    if (!ctx) {
        dl_log(DEBUG, "unknown socket");
        return -1;
    }

    _anjay_coap_socket_set_tx_params(get_anjay(dl)->coap_socket,
                                     &get_anjay(dl)->udp_tx_params);
    handle_message(dl, ctx);
    return 0;
}

#ifdef ANJAY_TEST
#include "test/downloader_mock.h"
#endif // ANJAY_TEST

static size_t get_max_acceptable_block_size(size_t in_buffer_size) {
    size_t estimated_response_header_size = sizeof(anjay_coap_msg_header_t)
            + ANJAY_COAP_MAX_TOKEN_LENGTH
            + ANJAY_COAP_OPT_ETAG_MAX_SIZE
            + ANJAY_COAP_OPT_BLOCK_MAX_SIZE
            + 1; // payload marker
    size_t payload_capacity = in_buffer_size - estimated_response_header_size;
    size_t block_size = _anjay_max_power_of_2_not_greater_than(payload_capacity);

    if (block_size > ANJAY_COAP_MSG_BLOCK_MAX_SIZE) {
        block_size = ANJAY_COAP_MSG_BLOCK_MAX_SIZE;
    }

    dl_log(TRACE, "input buffer size: %zu; max acceptable block size: %zu",
           in_buffer_size, block_size);
    return block_size;
}

static uintptr_t find_free_id(anjay_downloader_t *dl) {
    uintptr_t id;

    // One could think this can loop forever if all download IDs are in use.
    // However, uintptr_t is an integer as large as a pointer, and a normal
    // pointer needs to be able to address every byte that may be allocated
    // by malloc(). Since we use more than one byte per download object,
    // we can safely assume we will run out of RAM before running out
    // of download IDs.
    do {
        id = dl->next_id++;
    } while (id == INVALID_DOWNLOAD_ID || find_ctx_ptr_by_id(dl, id) != NULL);

    return id;
}

static int init_dl_ctx(anjay_downloader_t *dl,
                       anjay_download_ctx_t *ctx,
                       const anjay_download_config_t *cfg) {
    if (_anjay_parse_url(cfg->url, &ctx->uri)) {
        dl_log(ERROR, "invalid URL: %s", cfg->url);
        return -1;
    }

    if (!cfg->on_next_block || !cfg->on_download_finished) {
        dl_log(ERROR, "invalid download config: handlers not set up");
        return -1;
    }

    avs_net_ssl_configuration_t ssl_config = {
        .version = get_anjay(dl)->dtls_version,
        .backend_configuration = get_anjay(dl)->udp_socket_config,
        .security = cfg->security_info
    };
    ssl_config.backend_configuration.reuse_addr = 1;

    avs_net_socket_type_t socket_type;
    const void *config;
    if (!strcasecmp(ctx->uri.protocol, "coap")) {
        socket_type = AVS_NET_UDP_SOCKET;
        config = (const void *) &ssl_config.backend_configuration;
    } else if (!strcasecmp(ctx->uri.protocol, "coaps")) {
        socket_type = AVS_NET_DTLS_SOCKET;
        config = (const void *) &ssl_config;
    } else {
        dl_log(ERROR, "unsupported protocol: %s", ctx->uri.protocol);
        return -1;
    }

    // Downloader sockets MUST NOT reuse the same local port as LwM2M sockets.
    // If they do, and the client attempts to download anything from the same
    // host:port as is used by an LwM2M server, we will get two sockets with
    // identical local/remote host/port tuples. Depending on the socket
    // implementation, we may not be able to create such socket, packets might
    // get duplicated between these "identical" sockets, or we may get some
    // kind of load-balancing behavior. In the last case, the client would
    // randomly handle or ignore LwM2M requests and CoAP download responses.
    ctx->socket = _anjay_create_connected_udp_socket(get_anjay(dl), socket_type,
                                                     NULL, config, &ctx->uri);
    if (!ctx->socket) {
        dl_log(ERROR, "could not create CoAP socket");
        return -1;
    }

    ctx->id = find_free_id(dl);
    ctx->bytes_downloaded = cfg->start_offset;
    ctx->block_size = get_max_acceptable_block_size(get_anjay(dl)->in_buffer_size);
    ctx->etag = cfg->etag;
    ctx->on_next_block = cfg->on_next_block;
    ctx->on_download_finished = cfg->on_download_finished;
    ctx->user_data = cfg->user_data;

    return 0;
}

anjay_download_handle_t
_anjay_downloader_download(anjay_downloader_t *dl,
                           const anjay_download_config_t *config) {
    assert(&get_anjay(dl)->downloader == dl);

    AVS_LIST(anjay_download_ctx_t) dl_ctx =
            AVS_LIST_NEW_ELEMENT(anjay_download_ctx_t);
    if (!dl_ctx) {
        dl_log(ERROR, "out of memory");
        return (anjay_download_handle_t) INVALID_DOWNLOAD_ID;
    }

    if (init_dl_ctx(dl, dl_ctx, config)) {
        cleanup_transfer(dl, &dl_ctx);
        return (anjay_download_handle_t) INVALID_DOWNLOAD_ID;
    }

    AVS_LIST_INSERT(&dl->downloads, dl_ctx);

    if (_anjay_sched_now(get_anjay(dl)->sched, &dl_ctx->sched_job,
                         request_next_block_job, (void *) dl_ctx->id)) {
        dl_log(ERROR, "could not schedule download job");
        cleanup_transfer(dl, &dl->downloads);
        return (anjay_download_handle_t) INVALID_DOWNLOAD_ID;
    }

    assert(dl_ctx->id != INVALID_DOWNLOAD_ID);
    dl_log(INFO, "download scheduled: %s", config->url);
    return (anjay_download_handle_t) dl_ctx->id;
}

void _anjay_downloader_abort(anjay_downloader_t *dl,
                             anjay_download_handle_t handle) {
    uintptr_t id = (uintptr_t)handle;

    AVS_LIST(anjay_download_ctx_t) *ctx = find_ctx_ptr_by_id(dl, id);
    if (!ctx) {
        dl_log(DEBUG, "download id = %" PRIuPTR " not found (expired?)", id);
    } else {
        abort_transfer(dl, ctx, ANJAY_DOWNLOAD_ERR_ABORTED);
    }
}

#ifdef ANJAY_TEST
#include "test/downloader.c"
#endif // ANJAY_TEST
