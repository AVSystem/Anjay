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

#include <anjay_init.h>

#ifdef ANJAY_WITH_HTTP_DOWNLOAD

#    include <inttypes.h>

#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_http.h>
#    include <avsystem/commons/avs_memory.h>
#    include <avsystem/commons/avs_stream_net.h>
#    include <avsystem/commons/avs_utils.h>

#    define ANJAY_DOWNLOADER_INTERNALS

#    include "anjay_private.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    anjay_download_ctx_common_t common;
    avs_net_ssl_configuration_t ssl_configuration;
    avs_net_resolved_endpoint_t preferred_endpoint;
    avs_http_t *client;
    avs_url_t *parsed_url;
    avs_stream_t *stream;
    avs_sched_handle_t next_action_job;

    // State related to download resumption:
    anjay_etag_t *etag;
    size_t bytes_downloaded; // current offset in the remote resource
    size_t bytes_written;    // current offset in the local file
    // Note that the two values above may be different, for example when
    // we request Range: bytes=1200-, but the server responds with
    // Content-Range: bytes 1024-..., because it insists on using regular block
    // boundaries; we would then need to ignore 176 bytes without writing them.
} anjay_http_download_ctx_t;

static int read_start_byte_from_content_range(const char *content_range,
                                              uint64_t *out_start_byte) {
    uint64_t end_byte;
    long long complete_length;
    int after_slash = 0;
    if (avs_match_token(&content_range, "bytes", AVS_SPACES)
            || sscanf(content_range, "%" SCNu64 "-%" SCNu64 "/%n",
                      out_start_byte, &end_byte, &after_slash)
                           < 2
            || after_slash <= 0) {
        return -1;
    }
    return (strcmp(&content_range[after_slash], "*") == 0
            || (!_anjay_safe_strtoll(&content_range[after_slash],
                                     &complete_length)
                && complete_length >= 1
                && (uint64_t) (complete_length - 1) == end_byte))
                   ? 0
                   : -1;
}

static anjay_etag_t *read_etag(const char *text) {
    size_t len = strlen(text);
    if (len < 2 || len > UINT8_MAX + 2 || text[0] != '"'
            || text[len - 1] != '"') {
        return NULL;
    }
    anjay_etag_t *result = (anjay_etag_t *) avs_malloc(
            offsetof(anjay_etag_t, value) + (len - 2));
    if (result) {
        result->size = (uint8_t) (len - 2);
        memcpy(result->value, &text[1], result->size);
    }
    return result;
}

static inline bool etag_matches(const anjay_etag_t *etag, const char *text) {
    size_t len = strlen(text);
    return len == (size_t) (etag->size + 2) && text[0] == '"'
           && text[len - 1] == '"'
           && memcmp(etag->value, &text[1], etag->size) == 0;
}

static void
handle_http_packet_with_locked_buffer(anjay_t *anjay,
                                      AVS_LIST(anjay_download_ctx_t) *ctx_ptr,
                                      uint8_t *buffer) {
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) *ctx_ptr;
    bool nonblock_read_ready;
    do {
        size_t bytes_read;
        bool message_finished = false;

        avs_error_t err =
                avs_stream_read(ctx->stream, &bytes_read, &message_finished,
                                buffer, anjay->in_shared_buffer->capacity);
        if (avs_is_err(err)) {
            _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                             _anjay_download_status_failed(
                                                     err));
            return;
        }
        if (bytes_read) {
            assert(ctx->bytes_written >= ctx->bytes_downloaded);
            if (ctx->bytes_downloaded + bytes_read > ctx->bytes_written) {
                size_t bytes_to_write =
                        ctx->bytes_downloaded + bytes_read - ctx->bytes_written;
                assert(bytes_read >= bytes_to_write);
                if (avs_is_err((err = ctx->common.on_next_block(
                                        anjay,
                                        &buffer[bytes_read - bytes_to_write],
                                        bytes_to_write, ctx->etag,
                                        ctx->common.user_data)))) {
                    _anjay_downloader_abort_transfer(
                            &anjay->downloader, ctx_ptr,
                            _anjay_download_status_failed(err));
                    return;
                }
                ctx->bytes_written += bytes_to_write;
            }
            ctx->bytes_downloaded += bytes_read;
        }
        if (message_finished) {
            dl_log(INFO, _("HTTP transfer id = ") "%" PRIuPTR _(" finished"),
                   ctx->common.id);
            _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                             _anjay_download_status_success());
            return;
        }
        nonblock_read_ready = avs_stream_nonblock_read_ready(ctx->stream);
    } while (nonblock_read_ready);
    int result = AVS_RESCHED_DELAYED(&ctx->next_action_job,
                                     AVS_NET_SOCKET_DEFAULT_RECV_TIMEOUT);
    assert(!result);
    (void) result;
}

static void handle_http_packet(anjay_downloader_t *dl,
                               AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    anjay_t *anjay = _anjay_downloader_get_anjay(dl);
    uint8_t *buffer = avs_shared_buffer_acquire(anjay->in_shared_buffer);
    assert(buffer);
    handle_http_packet_with_locked_buffer(anjay, ctx_ptr, buffer);
    avs_shared_buffer_release(anjay->in_shared_buffer);
}

static void timeout_job(avs_sched_t *sched, const void *id_ptr) {
    anjay_t *anjay = _anjay_get_from_sched(sched);
    uintptr_t id = *(const uintptr_t *) id_ptr;
    AVS_LIST(anjay_download_ctx_t) *ctx_ptr =
            _anjay_downloader_find_ctx_ptr_by_id(&anjay->downloader, id);
    if (!ctx_ptr) {
        dl_log(DEBUG, _("download id = ") "%" PRIuPTR _("expired"), id);
        return;
    }

    _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                     _anjay_download_status_failed(
                                             avs_errno(AVS_ETIMEDOUT)));
}

static void send_request(avs_sched_t *sched, const void *id_ptr) {
    anjay_t *anjay = _anjay_get_from_sched(sched);
    uintptr_t id = *(const uintptr_t *) id_ptr;
    AVS_LIST(anjay_download_ctx_t) *ctx_ptr =
            _anjay_downloader_find_ctx_ptr_by_id(&anjay->downloader, id);
    if (!ctx_ptr) {
        dl_log(DEBUG, _("download id = ") "%" PRIuPTR _("expired"), id);
        return;
    }

    AVS_LIST(const avs_http_header_t) received_headers = NULL;
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) *ctx_ptr;
    avs_error_t err =
            avs_http_open_stream(&ctx->stream, ctx->client, AVS_HTTP_GET,
                                 AVS_HTTP_CONTENT_IDENTITY, ctx->parsed_url,
                                 NULL, NULL);
    if (avs_is_err(err) || !ctx->stream) {
        _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                         _anjay_download_status_failed(err));
        return;
    }

    avs_http_set_header_storage(ctx->stream, &received_headers);

    char ifmatch[258];
    if (ctx->etag) {
        if (avs_simple_snprintf(ifmatch, sizeof(ifmatch), "\"%.*s\"",
                                (int) ctx->etag->size, ctx->etag->value)
                        < 0
                || avs_http_add_header(ctx->stream, "If-Match", ifmatch)) {
            dl_log(ERROR, _("Could not send If-Match header"));
            _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                             _anjay_download_status_failed(
                                                     avs_errno(AVS_ENOMEM)));
            return;
        }
    }

    // see docs on UINT_STR_BUF_SIZE in Commons for details on this formula
    char range[sizeof("bytes=-") + (12 * sizeof(size_t)) / 5 + 1];
    if (ctx->bytes_written > 0) {
        if (avs_simple_snprintf(range, sizeof(range), "bytes=%lu-",
                                (unsigned long) ctx->bytes_written)
                        < 0
                || avs_http_add_header(ctx->stream, "Range", range)) {
            dl_log(ERROR, _("Could not resume HTTP download: could not send "
                            "Range header"));
            _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                             _anjay_download_status_failed(
                                                     avs_errno(AVS_ENOMEM)));
            return;
        }
    }

    if (avs_is_err((err = avs_stream_finish_message(ctx->stream)))) {
        int http_status = 200;
        if (err.category == AVS_HTTP_ERROR_CATEGORY) {
            http_status = avs_http_status_code(ctx->stream);
        }
        if (http_status < 200 || http_status >= 300) {
            dl_log(WARNING, _("HTTP error code ") "%d" _(" received"),
                   http_status);
            if (http_status == 412) { // Precondition Failed
                _anjay_downloader_abort_transfer(
                        &anjay->downloader, ctx_ptr,
                        _anjay_download_status_expired());
            } else {
                _anjay_downloader_abort_transfer(
                        &anjay->downloader, ctx_ptr,
                        _anjay_download_status_invalid_response(http_status));
            }
        } else {
            dl_log(ERROR, _("Could not send HTTP request: ") "%s",
                   AVS_COAP_STRERROR(err));
            _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                             _anjay_download_status_failed(
                                                     err));
        }
        return;
    }

    AVS_LIST(const avs_http_header_t) it;
    AVS_LIST_FOREACH(it, received_headers) {
        if (avs_strcasecmp(it->key, "Content-Range") == 0) {
            uint64_t bytes_downloaded;
            if (read_start_byte_from_content_range(it->value, &bytes_downloaded)
                    || bytes_downloaded > ctx->bytes_written) {
                dl_log(ERROR,
                       _("Could not resume HTTP download: invalid "
                         "Content-Range: ") "%s",
                       it->value);
                _anjay_downloader_abort_transfer(
                        &anjay->downloader, ctx_ptr,
                        _anjay_download_status_failed(avs_errno(AVS_EPROTO)));
                return;
            }
            ctx->bytes_downloaded = (size_t) bytes_downloaded;
        } else if (avs_strcasecmp(it->key, "ETag") == 0) {
            if (ctx->etag) {
                if (!etag_matches(ctx->etag, it->value)) {
                    dl_log(ERROR, _("ETag does not match"));
                    _anjay_downloader_abort_transfer(
                            &anjay->downloader, ctx_ptr,
                            _anjay_download_status_expired());
                    return;
                }
            } else if (!(ctx->etag = read_etag(it->value))) {
                dl_log(WARNING,
                       _("Could not store ETag of the download: ") "%s",
                       it->value);
            }
        }
    }
    avs_http_set_header_storage(ctx->stream, NULL);

    if (AVS_SCHED_DELAYED(anjay->sched, &ctx->next_action_job,
                          AVS_NET_SOCKET_DEFAULT_RECV_TIMEOUT, timeout_job,
                          &ctx->common.id, sizeof(ctx->common.id))) {
        dl_log(ERROR, _("could not schedule timeout job"));
        _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                         _anjay_download_status_failed(
                                                 avs_errno(AVS_ENOMEM)));
        return;
    }

    /*
     * If the whole downloaded file is small enough and is received before
     * we end handling HTTP headers, it may be read by the underlying
     * buffered_netstream alongside the last chunk of HTTP headers. In that
     * case, the poll()/select() recommended for use in main program loop
     * will never report data being available on the download socket, even
     * though we have *some* data cached in the buffered_netstream internal
     * buffer. We avoid this case by explicitly handling any buffered data
     * here.
     *
     * Also, we must not call handle_http_packet unconditionally, because if
     * there is no data buffered, the call would block waiting until a first
     * chunk of data is received from the server.
     */
    if (avs_stream_nonblock_read_ready(ctx->stream)) {
        handle_http_packet(&anjay->downloader, ctx_ptr);
    }
}

static int get_http_socket(anjay_downloader_t *dl,
                           anjay_download_ctx_t *ctx,
                           avs_net_socket_t **out_socket,
                           anjay_socket_transport_t *out_transport) {
    (void) dl;
    if (!(*out_socket = avs_stream_net_getsock(
                  ((anjay_http_download_ctx_t *) ctx)->stream))) {
        return -1;
    }
    *out_transport = ANJAY_SOCKET_TRANSPORT_TCP;
    return 0;
}

static void cleanup_http_transfer(AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) *ctx_ptr;
    avs_sched_del(&ctx->next_action_job);
    avs_free(ctx->etag);
    avs_stream_cleanup(&ctx->stream);
    avs_url_free(ctx->parsed_url);
    avs_free(ctx->ssl_configuration.ciphersuites.ids);
    avs_http_free(ctx->client);
    AVS_LIST_DELETE(ctx_ptr);
}

static avs_error_t
reconnect_http_transfer(anjay_downloader_t *dl,
                        AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) *ctx_ptr;
    avs_stream_cleanup(&ctx->stream);
    anjay_t *anjay = _anjay_downloader_get_anjay(dl);
    if (AVS_SCHED_NOW(anjay->sched, &ctx->next_action_job, send_request,
                      &ctx->common.id, sizeof(ctx->common.id))) {
        dl_log(ERROR, _("could not schedule download job"));
        return avs_errno(AVS_ENOMEM);
    }
    return AVS_OK;
}

avs_error_t
_anjay_downloader_http_ctx_new(anjay_downloader_t *dl,
                               AVS_LIST(anjay_download_ctx_t) *out_dl_ctx,
                               const anjay_download_config_t *cfg,
                               uintptr_t id) {
    anjay_t *anjay = _anjay_downloader_get_anjay(dl);
    AVS_LIST(anjay_http_download_ctx_t) ctx =
            AVS_LIST_NEW_ELEMENT(anjay_http_download_ctx_t);
    if (!ctx) {
        dl_log(ERROR, _("out of memory"));
        return avs_errno(AVS_ENOMEM);
    }

    static const anjay_download_ctx_vtable_t VTABLE = {
        .get_socket = get_http_socket,
        .handle_packet = handle_http_packet,
        .cleanup = cleanup_http_transfer,
        .reconnect = reconnect_http_transfer
    };
    ctx->common.vtable = &VTABLE;

    avs_http_buffer_sizes_t http_buffer_sizes = AVS_HTTP_DEFAULT_BUFFER_SIZES;
    if (cfg->start_offset > 0) {
        // prevent sending Accept-Encoding
        http_buffer_sizes.content_coding_input = 0;
    }

    avs_error_t err = AVS_OK;
    if (!(ctx->client = avs_http_new(&http_buffer_sizes))
            || _anjay_copy_tls_ciphersuites(
                       &ctx->ssl_configuration.ciphersuites,
                       cfg->security_config.tls_ciphersuites.num_ids
                               ? &cfg->security_config.tls_ciphersuites
                               : &anjay->default_tls_ciphersuites)) {
        err = avs_errno(AVS_ENOMEM);
        goto error;
    }
    ctx->ssl_configuration.security = cfg->security_config.security_info;
    ctx->ssl_configuration.backend_configuration.preferred_endpoint =
            &ctx->preferred_endpoint;
    ctx->ssl_configuration.prng_ctx = anjay->prng_ctx.ctx;
    avs_http_ssl_configuration(ctx->client, &ctx->ssl_configuration);

    if (!(ctx->parsed_url = avs_url_parse(cfg->url))) {
        err = avs_errno(AVS_EINVAL);
        goto error;
    }

    ctx->common.id = id;
    ctx->common.on_next_block = cfg->on_next_block;
    ctx->common.on_download_finished = cfg->on_download_finished;
    ctx->common.user_data = cfg->user_data;
    ctx->bytes_written = cfg->start_offset;
    if (cfg->etag) {
        size_t struct_size = offsetof(anjay_etag_t, value) + cfg->etag->size;
        if (!(ctx->etag = (anjay_etag_t *) avs_malloc(struct_size))) {
            dl_log(ERROR, _("could not copy ETag"));
            err = avs_errno(AVS_ENOMEM);
            goto error;
        }
        memcpy(ctx->etag, cfg->etag, struct_size);
    }

    if (AVS_SCHED_NOW(_anjay_downloader_get_anjay(dl)->sched,
                      &ctx->next_action_job, send_request, &ctx->common.id,
                      sizeof(ctx->common.id))) {
        dl_log(ERROR, _("could not schedule download job"));
        err = avs_errno(AVS_ENOMEM);
        goto error;
    }

    *out_dl_ctx = (AVS_LIST(anjay_download_ctx_t)) ctx;
    return AVS_OK;
error:
    cleanup_http_transfer((AVS_LIST(anjay_download_ctx_t) *) &ctx);
    assert(avs_is_err(err));
    return err;
}

#endif // ANJAY_WITH_HTTP_DOWNLOAD
