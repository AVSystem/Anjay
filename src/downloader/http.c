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

#include <avsystem/commons/errno.h>
#include <avsystem/commons/http.h>
#include <avsystem/commons/memory.h>
#include <avsystem/commons/stream/stream_net.h>
#include <avsystem/commons/utils.h>

#define ANJAY_DOWNLOADER_INTERNALS

#include "private.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    anjay_download_ctx_common_t common;
    avs_net_ssl_configuration_t ssl_configuration;
    avs_net_resolved_endpoint_t preferred_endpoint;
    avs_http_t *client;
    avs_url_t *parsed_url;
    avs_stream_abstract_t *stream;
    anjay_sched_handle_t send_request_job;

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

static void handle_http_packet(anjay_downloader_t *dl,
                               AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) *ctx_ptr;
    anjay_t *anjay = _anjay_downloader_get_anjay(dl);

    int nonblock_read_ready;
    do {
        size_t bytes_read;
        char message_finished = 0;
        if (avs_stream_read(ctx->stream, &bytes_read, &message_finished,
                            anjay->in_buffer, anjay->in_buffer_size)) {
            _anjay_downloader_abort_transfer(dl, ctx_ptr,
                                             ANJAY_DOWNLOAD_ERR_FAILED,
                                             avs_stream_errno(ctx->stream));
            return;
        }
        if (bytes_read) {
            assert(ctx->bytes_written >= ctx->bytes_downloaded);
            if (ctx->bytes_downloaded + bytes_read > ctx->bytes_written) {
                size_t bytes_to_write =
                        ctx->bytes_downloaded + bytes_read - ctx->bytes_written;
                assert(bytes_read >= bytes_to_write);
                if (ctx->common.on_next_block(
                            anjay,
                            &anjay->in_buffer[bytes_read - bytes_to_write],
                            bytes_to_write, ctx->etag, ctx->common.user_data)) {
                    _anjay_downloader_abort_transfer(
                            dl, ctx_ptr, ANJAY_DOWNLOAD_ERR_FAILED, errno);
                    return;
                }
                ctx->bytes_written += bytes_to_write;
            }
            ctx->bytes_downloaded += bytes_read;
        }
        if (message_finished) {
            dl_log(INFO, "HTTP transfer id = %" PRIuPTR " finished",
                   ctx->common.id);
            _anjay_downloader_abort_transfer(dl, ctx_ptr, 0, 0);
            return;
        }
        if ((nonblock_read_ready = avs_stream_nonblock_read_ready(ctx->stream))
                < 0) {
            _anjay_downloader_abort_transfer(dl, ctx_ptr,
                                             ANJAY_DOWNLOAD_ERR_FAILED, EIO);
            return;
        }
    } while (nonblock_read_ready > 0);
}

static void send_request(anjay_t *anjay, const void *id_ptr) {
    int error_code = ANJAY_DOWNLOAD_ERR_FAILED;
    uintptr_t id = *(const uintptr_t *) id_ptr;
    AVS_LIST(anjay_download_ctx_t) *ctx_ptr =
            _anjay_downloader_find_ctx_ptr_by_id(&anjay->downloader, id);
    if (!ctx_ptr) {
        dl_log(DEBUG, "download id = %" PRIuPTR "expired", id);
        return;
    }

    AVS_LIST(const avs_http_header_t) received_headers = NULL;
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) *ctx_ptr;
    int result = avs_http_open_stream(&ctx->stream, ctx->client, AVS_HTTP_GET,
                                      AVS_HTTP_CONTENT_IDENTITY,
                                      ctx->parsed_url, NULL, NULL);
    if (result || !ctx->stream) {
        goto error;
    }

    avs_http_set_header_storage(ctx->stream, &received_headers);

    char ifmatch[258];
    if (ctx->etag) {
        if (avs_simple_snprintf(ifmatch, sizeof(ifmatch), "\"%.*s\"",
                                (int) ctx->etag->size, ctx->etag->value)
                        < 0
                || avs_http_add_header(ctx->stream, "If-Match", ifmatch)) {
            dl_log(ERROR, "Could not send If-Match header");
            goto error;
        }
    }

    // see docs on UINT_STR_BUF_SIZE in Commons for details on this formula
    char range[sizeof("bytes=-") + (12 * sizeof(size_t)) / 5 + 1];
    if (ctx->bytes_written > 0) {
        if (avs_simple_snprintf(range, sizeof(range), "bytes=%lu-",
                                (unsigned long) ctx->bytes_written)
                        < 0
                || avs_http_add_header(ctx->stream, "Range", range)) {
            dl_log(ERROR, "Could not resume HTTP download: "
                          "could not send Range header");
            goto error;
        }
    }

    if (avs_stream_finish_message(ctx->stream)) {
        result = avs_stream_errno(ctx->stream);
        dl_log(ERROR, "Could not send HTTP request, error %d",
               avs_stream_errno(ctx->stream));
        if (result == 412) { // Precondition Failed
            error_code = ANJAY_DOWNLOAD_ERR_EXPIRED;
            result = ECONNABORTED;
        }
        goto error;
    }

    AVS_LIST(const avs_http_header_t) it;
    AVS_LIST_FOREACH(it, received_headers) {
        if (avs_strcasecmp(it->key, "Content-Range") == 0) {
            uint64_t bytes_downloaded;
            if (read_start_byte_from_content_range(it->value, &bytes_downloaded)
                    || bytes_downloaded > ctx->bytes_written) {
                dl_log(ERROR,
                       "Could not resume HTTP download: "
                       "invalid Content-Range: %s",
                       it->value);
                goto error;
            }
            ctx->bytes_downloaded = (size_t) bytes_downloaded;
        } else if (avs_strcasecmp(it->key, "ETag") == 0) {
            if (ctx->etag) {
                if (!etag_matches(ctx->etag, it->value)) {
                    dl_log(ERROR, "ETag does not match");
                    error_code = ANJAY_DOWNLOAD_ERR_EXPIRED;
                    result = ECONNABORTED;
                    goto error;
                }
            } else if (!(ctx->etag = read_etag(it->value))) {
                dl_log(ERROR, "Could not store ETag of the download");
                goto error;
            }
        }
    }
    avs_http_set_header_storage(ctx->stream, NULL);

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
    result = avs_stream_nonblock_read_ready(ctx->stream);
    if (result < 0) {
        error_code = ANJAY_DOWNLOAD_ERR_FAILED;
        result = avs_stream_errno(ctx->stream);
        goto error;
    } else if (result > 0) {
        handle_http_packet(&anjay->downloader, ctx_ptr);
    }
    return;
error:
    _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr, error_code,
                                     result);
}

static int get_http_socket(anjay_downloader_t *dl,
                           anjay_download_ctx_t *ctx,
                           avs_net_abstract_socket_t **out_socket,
                           anjay_socket_transport_t *out_transport) {
    (void) dl;
    if (!(*out_socket = avs_stream_net_getsock(
                  ((anjay_http_download_ctx_t *) ctx)->stream))) {
        return -1;
    }
    *out_transport = ANJAY_SOCKET_TRANSPORT_TCP;
    return 0;
}

static void cleanup_http_transfer(anjay_downloader_t *dl,
                                  AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    (void) dl;
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) *ctx_ptr;
    _anjay_sched_del(_anjay_downloader_get_anjay(dl)->sched,
                     &ctx->send_request_job);
    avs_free(ctx->etag);
    avs_stream_cleanup(&ctx->stream);
    avs_url_free(ctx->parsed_url);
    avs_http_free(ctx->client);
    AVS_LIST_DELETE(ctx_ptr);
}

static int reconnect_http_transfer(anjay_downloader_t *dl,
                                   AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) *ctx_ptr;
    avs_stream_cleanup(&ctx->stream);
    anjay_t *anjay = _anjay_downloader_get_anjay(dl);
    _anjay_sched_del(anjay->sched, &ctx->send_request_job);
    if (_anjay_sched_now(anjay->sched, &ctx->send_request_job, send_request,
                         &ctx->common.id, sizeof(ctx->common.id))) {
        dl_log(ERROR, "could not schedule download job");
        return -ENOMEM;
    }
    return 0;
}

int _anjay_downloader_http_ctx_new(anjay_downloader_t *dl,
                                   AVS_LIST(anjay_download_ctx_t) *out_dl_ctx,
                                   const anjay_download_config_t *cfg,
                                   uintptr_t id) {
    AVS_LIST(anjay_http_download_ctx_t) ctx =
            AVS_LIST_NEW_ELEMENT(anjay_http_download_ctx_t);
    if (!ctx) {
        dl_log(ERROR, "out of memory");
        return -ENOMEM;
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

    int result = 0;
    if (!(ctx->client = avs_http_new(&http_buffer_sizes))) {
        result = -ENOMEM;
        goto error;
    }
    ctx->ssl_configuration.security = cfg->security_info;
    ctx->ssl_configuration.backend_configuration.preferred_endpoint =
            &ctx->preferred_endpoint;
    avs_http_ssl_configuration(ctx->client, &ctx->ssl_configuration);

    if (!(ctx->parsed_url = avs_url_parse(cfg->url))) {
        result = -EINVAL;
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
            dl_log(ERROR, "could not copy ETag");
            result = -ENOMEM;
            goto error;
        }
        memcpy(ctx->etag, cfg->etag, struct_size);
    }

    if (_anjay_sched_now(_anjay_downloader_get_anjay(dl)->sched,
                         &ctx->send_request_job, send_request, &ctx->common.id,
                         sizeof(ctx->common.id))) {
        dl_log(ERROR, "could not schedule download job");
        result = -ENOMEM;
        goto error;
    }

    *out_dl_ctx = (AVS_LIST(anjay_download_ctx_t)) ctx;
    return 0;
error:
    cleanup_http_transfer(dl, (AVS_LIST(anjay_download_ctx_t) *) &ctx);
    assert(result);
    return result;
}
