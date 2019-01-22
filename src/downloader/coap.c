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

#include <inttypes.h>

#include <avsystem/commons/coap/msg_builder.h>
#include <avsystem/commons/coap/msg_opt.h>
#include <avsystem/commons/errno.h>
#include <avsystem/commons/utils.h>

#define ANJAY_DOWNLOADER_INTERNALS

#include "private.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    uint8_t size;
    uint8_t value[8];
} anjay_coap_etag_t;

AVS_STATIC_ASSERT(offsetof(anjay_etag_t, value)
                          == offsetof(anjay_coap_etag_t, value),
                  coap_etag_layout_compatible);
AVS_STATIC_ASSERT(AVS_ALIGNOF(anjay_etag_t) == AVS_ALIGNOF(anjay_coap_etag_t),
                  coap_etag_alignment_compatible);

typedef struct {
    anjay_download_ctx_common_t common;

    anjay_url_t uri;
    size_t bytes_downloaded;
    size_t block_size;
    anjay_coap_etag_t etag;

    avs_net_abstract_socket_t *socket;
    avs_net_resolved_endpoint_t preferred_endpoint;
    char dtls_session_buffer[ANJAY_DTLS_SESSION_BUFFER_SIZE];
    avs_coap_msg_identity_t last_req_id;

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
    avs_coap_retry_state_t retry_state;
    avs_coap_tx_params_t tx_params;
} anjay_coap_download_ctx_t;

static void cleanup_coap_transfer(anjay_downloader_t *dl,
                                  AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    anjay_coap_download_ctx_t *ctx = (anjay_coap_download_ctx_t *) *ctx_ptr;
    _anjay_sched_del(_anjay_downloader_get_anjay(dl)->sched, &ctx->sched_job);
    _anjay_url_cleanup(&ctx->uri);
#ifndef ANJAY_TEST
    avs_net_socket_cleanup(&ctx->socket);
#endif // ANJAY_TEST
    AVS_LIST_DELETE(ctx_ptr);
}

static int fill_coap_request_info(avs_coap_msg_info_t *req_info,
                                  const anjay_coap_download_ctx_t *ctx) {
    req_info->type = AVS_COAP_MSG_CONFIRMABLE;
    req_info->code = AVS_COAP_CODE_GET;
    req_info->identity = ctx->last_req_id;

    AVS_LIST(const anjay_string_t) elem;
    AVS_LIST_FOREACH(elem, ctx->uri.uri_path) {
        if (avs_coap_msg_info_opt_string(req_info, AVS_COAP_OPT_URI_PATH,
                                         elem->c_str)) {
            return -1;
        }
    }
    AVS_LIST_FOREACH(elem, ctx->uri.uri_query) {
        if (avs_coap_msg_info_opt_string(req_info, AVS_COAP_OPT_URI_QUERY,
                                         elem->c_str)) {
            return -1;
        }
    }

    avs_coap_block_info_t block2 = {
        .type = AVS_COAP_BLOCK2,
        .valid = true,
        .seq_num = (uint32_t) (ctx->bytes_downloaded / ctx->block_size),
        .size = (uint16_t) ctx->block_size,
        .has_more = false
    };
    if (avs_coap_msg_info_opt_block(req_info, &block2)) {
        return -1;
    }

    return 0;
}

static void request_coap_block_job(anjay_t *anjay, const void *id_ptr);

static int schedule_coap_retransmission(anjay_downloader_t *dl,
                                        anjay_coap_download_ctx_t *ctx) {
    anjay_t *anjay = _anjay_downloader_get_anjay(dl);

    avs_coap_update_retry_state(&ctx->retry_state, &ctx->tx_params,
                                &dl->rand_seed);
    _anjay_sched_del(anjay->sched, &ctx->sched_job);
    return _anjay_sched(anjay->sched, &ctx->sched_job,
                        ctx->retry_state.recv_timeout, request_coap_block_job,
                        &ctx->common.id, sizeof(ctx->common.id));
}

static int request_coap_block(anjay_downloader_t *dl,
                              anjay_coap_download_ctx_t *ctx) {
    anjay_t *anjay = _anjay_downloader_get_anjay(dl);
    avs_coap_msg_info_t info = avs_coap_msg_info_init();
    const avs_coap_msg_t *msg = NULL;
    size_t required_storage_size;
    int result = -1;

    if (fill_coap_request_info(&info, (anjay_coap_download_ctx_t *) ctx)) {
        goto finish;
    }

    required_storage_size = avs_coap_msg_info_get_packet_storage_size(&info, 0);
    if (required_storage_size > anjay->out_buffer_size) {
        dl_log(ERROR,
               "CoAP output buffer too small to hold download request "
               "(at least %lu bytes is needed)",
               (unsigned long) required_storage_size);
        goto finish;
    }
    avs_coap_msg_builder_t builder;
    avs_coap_msg_builder_init(&builder,
                              avs_coap_ensure_aligned_buffer(anjay->out_buffer),
                              anjay->out_buffer_size, &info);

    msg = avs_coap_msg_builder_get_msg(&builder);

    result = avs_coap_ctx_send(anjay->coap_ctx, ctx->socket, msg);

    if (result) {
        dl_log(ERROR, "could not send request: %d", result);
    }

finish:
    avs_coap_msg_info_reset(&info);
    return result;
}

static void request_coap_block_job(anjay_t *anjay, const void *id_ptr) {
    uintptr_t id = *(const uintptr_t *) id_ptr;

    AVS_LIST(anjay_download_ctx_t) *ctx_ptr =
            _anjay_downloader_find_ctx_ptr_by_id(&anjay->downloader, id);
    if (!ctx_ptr) {
        dl_log(DEBUG, "download id = %" PRIuPTR " not found (expired?)", id);
        return;
    }

    anjay_coap_download_ctx_t *ctx = (anjay_coap_download_ctx_t *) *ctx_ptr;
    if (ctx->retry_state.retry_count > ctx->tx_params.max_retransmit) {
        dl_log(ERROR,
               "Limit of retransmissions reached, aborting download "
               "id = %" PRIuPTR,
               id);
        _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                         ANJAY_DOWNLOAD_ERR_FAILED, ETIMEDOUT);
    } else {
        request_coap_block(&anjay->downloader, ctx);
        if (schedule_coap_retransmission(&anjay->downloader, ctx)) {
            dl_log(WARNING,
                   "could not schedule retransmission for download "
                   "id = %" PRIuPTR,
                   ctx->common.id);
            _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                             ANJAY_DOWNLOAD_ERR_FAILED, ENOMEM);
        }
    }
}

static int map_coap_ctx_err_to_errno(int err) {
    switch (err) {
    case AVS_COAP_CTX_ERR_TIMEOUT:
        return ETIMEDOUT;
    case AVS_COAP_CTX_ERR_MSG_TOO_LONG:
        return EMSGSIZE;
    default:
        return ECONNRESET;
    }
}

static int request_next_coap_block(anjay_downloader_t *dl,
                                   AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    anjay_coap_download_ctx_t *ctx = (anjay_coap_download_ctx_t *) *ctx_ptr;
    ctx->last_req_id = _anjay_coap_id_source_get(dl->id_source);
    memset(&ctx->retry_state, 0, sizeof(ctx->retry_state));

    int result;
    if ((result = request_coap_block(dl, ctx))
            || (result = schedule_coap_retransmission(dl, ctx))) {
        dl_log(WARNING,
               "could not request block starting at %lu "
               "for download id = %" PRIuPTR,
               (unsigned long) ctx->bytes_downloaded, ctx->common.id);
        _anjay_downloader_abort_transfer(dl, ctx_ptr, ANJAY_DOWNLOAD_ERR_FAILED,
                                         map_coap_ctx_err_to_errno(result));
        return -1;
    }

    return 0;
}

static void request_next_coap_block_job(anjay_t *anjay, const void *id_ptr) {
    uintptr_t id = *(const uintptr_t *) id_ptr;
    AVS_LIST(anjay_download_ctx_t) *ctx =
            _anjay_downloader_find_ctx_ptr_by_id(&anjay->downloader, id);
    if (!ctx) {
        dl_log(DEBUG, "download id = %" PRIuPTR "expired", id);
    } else {
        request_next_coap_block(&anjay->downloader, ctx);
    }
}

static inline const char *
etag_to_string(char *buf, size_t buf_size, const anjay_coap_etag_t *etag) {
    AVS_ASSERT(buf_size >= sizeof(etag->value) * 3 + 1,
               "buffer too small to hold ETag");

    for (size_t i = 0; i < etag->size; ++i) {
        snprintf(&buf[i * 3], buf_size - i * 3, "%02x ", etag->value[i]);
    }

    return buf;
}

#define ETAG_STR(EtagPtr) etag_to_string(&(char[32]){ 0 }[0], 32, (EtagPtr))

static int read_etag(const avs_coap_msg_t *msg, anjay_coap_etag_t *out_etag) {
    const avs_coap_opt_t *etag_opt = NULL;
    int result =
            avs_coap_msg_find_unique_opt(msg, AVS_COAP_OPT_ETAG, &etag_opt);
    if (!etag_opt) {
        dl_log(TRACE, "no ETag option");
        out_etag->size = 0;
        return 0;
    }

    if (etag_opt && result) {
        dl_log(DEBUG, "multiple ETag options found");
        return -1;
    }

    uint32_t etag_size = avs_coap_opt_content_length(etag_opt);
    if (etag_size > sizeof(out_etag->value)) {
        dl_log(DEBUG, "invalid ETag option size");
        return -1;
    }

    out_etag->size = (uint8_t) etag_size;
    memcpy(out_etag->value, avs_coap_opt_value(etag_opt), out_etag->size);

    dl_log(TRACE, "ETag: %s", ETAG_STR(out_etag));
    return 0;
}

static inline bool etag_matches(const anjay_coap_etag_t *a,
                                const anjay_coap_etag_t *b) {
    return a->size == b->size && !memcmp(a->value, b->value, a->size);
}

static int parse_coap_response(const avs_coap_msg_t *msg,
                               anjay_coap_download_ctx_t *ctx,
                               avs_coap_block_info_t *out_block2,
                               anjay_coap_etag_t *out_etag) {
    if (read_etag(msg, out_etag)) {
        return -1;
    }

    int result = avs_coap_get_block_info(msg, AVS_COAP_BLOCK2, out_block2);
    if (result) {
        dl_log(DEBUG, "malformed response");
        return -1;
    }

    if (!out_block2->valid) {
        dl_log(DEBUG, "BLOCK2 option missing");
        return -1;
    }

    if (out_block2->has_more
            && out_block2->size != avs_coap_msg_payload_length(msg)) {
        dl_log(DEBUG, "malformed response: mismatched size of intermediate "
                      "packet");
        return -1;
    }

    const size_t requested_seq_num = ctx->bytes_downloaded / ctx->block_size;
    const size_t expected_offset = requested_seq_num * ctx->block_size;
    const size_t obtained_offset = out_block2->seq_num * out_block2->size;
    if (expected_offset != obtained_offset) {
        dl_log(DEBUG,
               "expected to get data from offset %lu but got %lu instead",
               (unsigned long) expected_offset,
               (unsigned long) obtained_offset);
        return -1;
    }

    if (out_block2->size > ctx->block_size) {
        dl_log(DEBUG,
               "block size renegotiation failed: requested %lu, got %" PRIu16,
               (unsigned long) ctx->block_size, out_block2->size);
        return -1;
    } else if (out_block2->size < ctx->block_size) {
        // Allow late block size renegotiation, as we may be in the middle of
        // a download resumption, in which case we have no idea what block size
        // is appropriate. If it is not the case, and the server decided to send
        // us smaller blocks instead, it won't hurt us to get them anyway.
        dl_log(DEBUG, "block size renegotiated: %lu -> %" PRIu16,
               (unsigned long) ctx->block_size, out_block2->size);
        ctx->block_size = out_block2->size;
    }

    return 0;
}

static void handle_coap_response(const avs_coap_msg_t *msg,
                                 anjay_downloader_t *dl,
                                 AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    const uint8_t code = avs_coap_msg_get_code(msg);
    if (code != AVS_COAP_CODE_CONTENT) {
        dl_log(DEBUG, "server responded with %s (expected %s)",
               AVS_COAP_CODE_STRING(code),
               AVS_COAP_CODE_STRING(AVS_COAP_CODE_CONTENT));
        _anjay_downloader_abort_transfer(dl, ctx_ptr, -code, ECONNREFUSED);
        return;
    }

    anjay_coap_download_ctx_t *ctx = (anjay_coap_download_ctx_t *) *ctx_ptr;
    avs_coap_block_info_t block2;
    anjay_coap_etag_t etag;
    if (parse_coap_response(msg, ctx, &block2, &etag)) {
        _anjay_downloader_abort_transfer(dl, ctx_ptr, ANJAY_DOWNLOAD_ERR_FAILED,
                                         EINVAL);
        return;
    }

    if (ctx->etag.size == 0) {
        ctx->etag = etag;
    } else if (!etag_matches(&etag, &ctx->etag)) {
        dl_log(DEBUG, "remote resource expired, aborting download");
        _anjay_downloader_abort_transfer(
                dl, ctx_ptr, ANJAY_DOWNLOAD_ERR_EXPIRED, ECONNABORTED);
        return;
    }

    const void *payload = avs_coap_msg_payload(msg);
    size_t payload_size = avs_coap_msg_payload_length(msg);

    // Resumption from a non-multiple block-size
    size_t offset = ctx->bytes_downloaded % ctx->block_size;
    if (offset) {
        payload = (const char *) payload + offset;
        payload_size -= offset;
    }

    if (ctx->common.on_next_block(_anjay_downloader_get_anjay(dl),
                                  (const uint8_t *) payload, payload_size,
                                  (const anjay_etag_t *) &etag,
                                  ctx->common.user_data)) {
        _anjay_downloader_abort_transfer(dl, ctx_ptr, ANJAY_DOWNLOAD_ERR_FAILED,
                                         errno);
        return;
    }

    ctx->bytes_downloaded += payload_size;
    if (!block2.has_more) {
        dl_log(INFO, "transfer id = %" PRIuPTR " finished", ctx->common.id);
        _anjay_downloader_abort_transfer(dl, ctx_ptr, 0, 0);
    } else if (!request_next_coap_block(dl, ctx_ptr)) {
        dl_log(TRACE, "transfer id = %" PRIuPTR ": %lu B downloaded",
               ctx->common.id, (unsigned long) ctx->bytes_downloaded);
    }
}

static void abort_transfer_job(anjay_t *anjay, const void *ctx_) {
    AVS_LIST(anjay_download_ctx_t) ctx =
            *(AVS_LIST(anjay_download_ctx_t) const *) ctx_;
    AVS_LIST(anjay_download_ctx_t) *ctx_ptr =
            // IAR compiler does not support typeof, so AVS_LIST_FIND_PTR
            // returns void**, which is not implicitly-convertible
            (AVS_LIST(anjay_download_ctx_t) *) AVS_LIST_FIND_PTR(
                    &anjay->downloader.downloads, ctx);

    if (!ctx_ptr) {
        anjay_log(WARNING, "transfer already aborted");
    } else {
        anjay_log(WARNING, "aborting download: response not received");
        _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                         ANJAY_DOWNLOAD_ERR_FAILED, ETIMEDOUT);
    }
}

static void handle_coap_message(anjay_downloader_t *dl,
                                AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    anjay_t *anjay = _anjay_downloader_get_anjay(dl);
    assert(ctx_ptr);
    assert(*ctx_ptr);

    avs_coap_msg_t *msg =
            (avs_coap_msg_t *) avs_coap_ensure_aligned_buffer(anjay->in_buffer);

    anjay_coap_download_ctx_t *ctx = (anjay_coap_download_ctx_t *) *ctx_ptr;

    int result = avs_coap_ctx_recv(anjay->coap_ctx, ctx->socket, msg,
                                   anjay->in_buffer_size);

    if (result) {
        dl_log(DEBUG, "recv result: %d", result);
        return;
    }

    bool msg_id_must_match = true;
    avs_coap_msg_type_t type = avs_coap_msg_get_type(msg);
    switch (type) {
    case AVS_COAP_MSG_RESET:
    case AVS_COAP_MSG_ACKNOWLEDGEMENT:
        break;
    case AVS_COAP_MSG_CONFIRMABLE:
        msg_id_must_match = false; // Separate Response
        break;
    case AVS_COAP_MSG_NON_CONFIRMABLE:
        dl_log(DEBUG, "unexpected msg type: %d, ignoring", (int) type);
        return;
    }

    if (!avs_coap_msg_token_matches(msg, &ctx->last_req_id)) {
        dl_log(DEBUG, "token mismatch, ignoring");
        return;
    }

    if (msg_id_must_match) {
        if (avs_coap_msg_get_id(msg) != ctx->last_req_id.msg_id) {
            dl_log(DEBUG, "msg id mismatch (got %u, expected %u), ignoring",
                   avs_coap_msg_get_id(msg), ctx->last_req_id.msg_id);
            return;
        } else if (type == AVS_COAP_MSG_RESET) {
            dl_log(DEBUG, "Reset response, aborting transfer");
            _anjay_downloader_abort_transfer(
                    dl, ctx_ptr, ANJAY_DOWNLOAD_ERR_FAILED, ECONNREFUSED);
            return;
        } else if (type == AVS_COAP_MSG_ACKNOWLEDGEMENT
                   && avs_coap_msg_get_code(msg) == AVS_COAP_CODE_EMPTY) {
            avs_time_duration_t abort_delay =
                    avs_coap_exchange_lifetime(&ctx->tx_params);
            dl_log(DEBUG,
                   "Separate ACK received, waiting "
                   "%" PRId64 ".%09" PRId32 " for response",
                   abort_delay.seconds, abort_delay.nanoseconds);

            _anjay_sched_del(anjay->sched, &ctx->sched_job);
            _anjay_sched(anjay->sched, &ctx->sched_job, abort_delay,
                         abort_transfer_job, ctx_ptr, sizeof(*ctx_ptr));
            return;
        }
    } else {
        dl_log(TRACE, "Separate Response received");
        avs_coap_ctx_send_empty(anjay->coap_ctx, ctx->socket,
                                AVS_COAP_MSG_ACKNOWLEDGEMENT,
                                avs_coap_msg_get_id(msg));
    }

    handle_coap_response(msg, dl, ctx_ptr);
}

static int get_coap_socket(anjay_downloader_t *dl,
                           anjay_download_ctx_t *ctx,
                           avs_net_abstract_socket_t **out_socket,
                           anjay_socket_transport_t *out_transport) {
    (void) dl;
    if (!(*out_socket = ((anjay_coap_download_ctx_t *) ctx)->socket)) {
        return -1;
    }
    *out_transport = ANJAY_SOCKET_TRANSPORT_UDP;
    return 0;
}

static size_t get_max_acceptable_block_size(size_t in_buffer_size) {
    size_t estimated_response_header_size =
            AVS_COAP_MAX_HEADER_SIZE + AVS_COAP_MAX_TOKEN_LENGTH
            + AVS_COAP_OPT_ETAG_MAX_SIZE + AVS_COAP_OPT_BLOCK_MAX_SIZE
            + 1; // payload marker
    size_t payload_capacity = in_buffer_size - estimated_response_header_size;
    size_t block_size =
            _anjay_max_power_of_2_not_greater_than(payload_capacity);

    if (block_size > AVS_COAP_MSG_BLOCK_MAX_SIZE) {
        block_size = AVS_COAP_MSG_BLOCK_MAX_SIZE;
    }

    dl_log(TRACE, "input buffer size: %lu; max acceptable block size: %lu",
           (unsigned long) in_buffer_size, (unsigned long) block_size);
    return block_size;
}

static int reconnect_coap_transfer(anjay_downloader_t *dl,
                                   AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    anjay_coap_download_ctx_t *ctx = (anjay_coap_download_ctx_t *) *ctx_ptr;
    char hostname[ANJAY_MAX_URL_HOSTNAME_SIZE];
    char port[ANJAY_MAX_URL_PORT_SIZE];
    if (avs_net_socket_get_remote_hostname(ctx->socket, hostname,
                                           sizeof(hostname))
            || avs_net_socket_get_remote_port(ctx->socket, port, sizeof(port))
            || avs_net_socket_close(ctx->socket)
            || avs_net_socket_connect(ctx->socket, hostname, port)) {
        dl_log(WARNING,
               "could not reconnect socket for download "
               "id = %" PRIuPTR,
               ctx->common.id);
        return -avs_net_socket_errno(ctx->socket);
    } else {
        anjay_t *anjay = _anjay_downloader_get_anjay(dl);
        _anjay_sched_del(anjay->sched, &ctx->sched_job);
        if (_anjay_sched_now(anjay->sched, &ctx->sched_job,
                             request_next_coap_block_job, &ctx->common.id,
                             sizeof(ctx->common.id))) {
            dl_log(WARNING,
                   "could not schedule resumption for download "
                   "id = %" PRIuPTR,
                   ctx->common.id);
            return -ENOMEM;
        }
    }
    return 0;
}

#ifdef ANJAY_TEST
#    include "test/downloader_mock.h"
#endif // ANJAY_TEST

int _anjay_downloader_coap_ctx_new(anjay_downloader_t *dl,
                                   AVS_LIST(anjay_download_ctx_t) *out_dl_ctx,
                                   const anjay_download_config_t *cfg,
                                   uintptr_t id) {
    anjay_t *anjay = _anjay_downloader_get_anjay(dl);
    assert(!*out_dl_ctx);
    AVS_LIST(anjay_coap_download_ctx_t) ctx =
            AVS_LIST_NEW_ELEMENT(anjay_coap_download_ctx_t);
    if (!ctx) {
        dl_log(ERROR, "out of memory");
        return -ENOMEM;
    }

    avs_net_ssl_configuration_t ssl_config;
    int result = 0;
    static const anjay_download_ctx_vtable_t VTABLE = {
        .get_socket = get_coap_socket,
        .handle_packet = handle_coap_message,
        .cleanup = cleanup_coap_transfer,
        .reconnect = reconnect_coap_transfer
    };
    ctx->common.vtable = &VTABLE;

    if (_anjay_url_parse(cfg->url, &ctx->uri)) {
        dl_log(ERROR, "invalid URL: %s", cfg->url);
        result = -EINVAL;
        goto error;
    }

    if (cfg->etag && cfg->etag->size > sizeof(ctx->etag.value)) {
        dl_log(ERROR, "ETag too long");
        goto error;
    }

    if (!cfg->on_next_block || !cfg->on_download_finished) {
        dl_log(ERROR, "invalid download config: handlers not set up");
        result = -EINVAL;
        goto error;
    }

    ssl_config = (avs_net_ssl_configuration_t) {
        .version = anjay->dtls_version,
        .security = cfg->security_info,
        .session_resumption_buffer = ctx->dtls_session_buffer,
        .session_resumption_buffer_size = sizeof(ctx->dtls_session_buffer),
        .backend_configuration = anjay->udp_socket_config
    };
    ssl_config.backend_configuration.reuse_addr = 1;
    ssl_config.backend_configuration.preferred_endpoint =
            &ctx->preferred_endpoint;

    avs_net_socket_type_t socket_type;
    const void *config;
    switch (ctx->uri.protocol) {
    case ANJAY_URL_PROTOCOL_COAP:
        socket_type = AVS_NET_UDP_SOCKET;
        config = (const void *) &ssl_config.backend_configuration;
        break;
    case ANJAY_URL_PROTOCOL_COAPS:
        socket_type = AVS_NET_DTLS_SOCKET;
        config = (const void *) &ssl_config;
        break;
    default:
        dl_log(ERROR, "unsupported protocol ID: %d", (int) ctx->uri.protocol);
        result = -EPROTONOSUPPORT;
        goto error;
    }

    // Downloader sockets MUST NOT reuse the same local port as LwM2M sockets.
    // If they do, and the client attempts to download anything from the same
    // host:port as is used by an LwM2M server, we will get two sockets with
    // identical local/remote host/port tuples. Depending on the socket
    // implementation, we may not be able to create such socket, packets might
    // get duplicated between these "identical" sockets, or we may get some
    // kind of load-balancing behavior. In the last case, the client would
    // randomly handle or ignore LwM2M requests and CoAP download responses.
    if (avs_net_socket_create(&ctx->socket, socket_type, config)) {
        dl_log(ERROR, "could not create CoAP socket");
        result = -ENOMEM;
    } else if (avs_net_socket_connect(ctx->socket, ctx->uri.host,
                                      ctx->uri.port)) {
        dl_log(ERROR, "could not connect CoAP socket");
        if (!(result = -avs_net_socket_errno(ctx->socket))) {
            result = -EPROTO;
        }
        avs_net_socket_cleanup(&ctx->socket);
    }
    if (!ctx->socket) {
        dl_log(ERROR, "could not create CoAP socket");
        goto error;
    }

    ctx->common.id = id;
    ctx->common.on_next_block = cfg->on_next_block;
    ctx->common.on_download_finished = cfg->on_download_finished;
    ctx->common.user_data = cfg->user_data;
    ctx->bytes_downloaded = cfg->start_offset;
    ctx->block_size = get_max_acceptable_block_size(anjay->in_buffer_size);
    if (cfg->etag) {
        ctx->etag.size = cfg->etag->size;
        memcpy(ctx->etag.value, cfg->etag->value, ctx->etag.size);
    }

    if (!cfg->coap_tx_params) {
        ctx->tx_params = anjay->udp_tx_params;
    } else {
        const char *error_string = NULL;
        if (avs_coap_tx_params_valid(cfg->coap_tx_params, &error_string)) {
            ctx->tx_params = *cfg->coap_tx_params;
        } else {
            dl_log(ERROR, "invalid tx_params: %s", error_string);
            goto error;
        }
    }

    if (_anjay_sched_now(anjay->sched,
                         &ctx->sched_job,
                         request_next_coap_block_job,
                         &ctx->common.id,
                         sizeof(ctx->common.id))) {
        dl_log(ERROR, "could not schedule download job");
        result = -ENOMEM;
        goto error;
    }

    *out_dl_ctx = (AVS_LIST(anjay_download_ctx_t)) ctx;
    return 0;
error:
    cleanup_coap_transfer(dl, (AVS_LIST(anjay_download_ctx_t) *) &ctx);
    return result;
}

#ifdef ANJAY_TEST
#    include "test/downloader.c"
#endif // ANJAY_TEST
