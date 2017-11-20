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

#include <errno.h>
#include <inttypes.h>

#include <avsystem/commons/http.h>
#include <avsystem/commons/stream/stream_net.h>

#define ANJAY_DOWNLOADER_INTERNALS

#include "private.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    anjay_download_ctx_common_t common;
    avs_net_ssl_configuration_t ssl_configuration;
    avs_http_t *client;
    avs_url_t *parsed_url;
    avs_stream_abstract_t *stream;
    anjay_sched_handle_t send_request_job;
} anjay_http_download_ctx_t;

static int send_request(anjay_t *anjay, void *id_) {
    uintptr_t id = (uintptr_t) id_;
    AVS_LIST(anjay_download_ctx_t) *ctx_ptr =
            _anjay_downloader_find_ctx_ptr_by_id(&anjay->downloader, id);
    if (!ctx_ptr) {
        dl_log(DEBUG, "download id = %" PRIuPTR "expired", id);
        return 0;
    }

    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) *ctx_ptr;
    int result = avs_http_open_stream(&ctx->stream, ctx->client,
                                      AVS_HTTP_GET, AVS_HTTP_CONTENT_IDENTITY,
                                      ctx->parsed_url, NULL, NULL);
    avs_url_free(ctx->parsed_url);
    ctx->parsed_url = NULL;
    if (result || !ctx->stream) {
        goto error;
    }

    if (avs_stream_finish_message(ctx->stream)) {
        result = avs_stream_errno(ctx->stream);
        dl_log(ERROR, "Could not send HTTP request, error %d",
               avs_stream_errno(ctx->stream));
        goto error;
    }

    return 0;
error:
    _anjay_downloader_abort_transfer(&anjay->downloader, ctx_ptr,
                                     ANJAY_DOWNLOAD_ERR_FAILED, result);
    return 0;
}

static avs_net_abstract_socket_t *get_http_socket(anjay_downloader_t *dl,
                                                  anjay_download_ctx_t *ctx) {
    (void) dl;
    return avs_stream_net_getsock(((anjay_http_download_ctx_t *) ctx)->stream);
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
        if (bytes_read
                && ctx->common.on_next_block(_anjay_downloader_get_anjay(dl),
                                             anjay->in_buffer, bytes_read, NULL,
                                             ctx->common.user_data)) {
            _anjay_downloader_abort_transfer(dl, ctx_ptr,
                                             ANJAY_DOWNLOAD_ERR_FAILED, errno);
            return;
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

static void cleanup_http_transfer(anjay_downloader_t *dl,
                                  AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    (void) dl;
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) *ctx_ptr;
    if (ctx->send_request_job) {
        _anjay_sched_del(_anjay_downloader_get_anjay(dl)->sched,
                         &ctx->send_request_job);
    }
    avs_stream_cleanup(&ctx->stream);
    avs_url_free(ctx->parsed_url);
    avs_http_free(ctx->client);
    AVS_LIST_DELETE(ctx_ptr);
}

int _anjay_downloader_http_ctx_new(anjay_downloader_t *dl,
                                   AVS_LIST(anjay_download_ctx_t) *out_dl_ctx,
                                   const anjay_download_config_t *cfg,
                                   uintptr_t id) {
    if (cfg->start_offset > 0) {
        dl_log(ERROR, "resuming downloads not currently supported for HTTP");
        return -ENOTSUP;
        // TODO: Actually implement this. This is more work than it might look
        // at first glance. We'd need to:
        // - Send the "Range: bytes=<start_offset>-" HTTP header
        // - Read the Content-Range response header, which might actually be
        //   different from the requested range (might include bytes before what
        //   we actually want, much like block-aligned requests in CoAP; or it
        //   might even be missing completely, which means that the server sent
        //   full body anyway
        // - Prevent Accept-Encoding from being sent (or even better, send
        //   "Accept-Encoding: identity") - HTTP content compression does not
        //   play well with partial requests, because values in Range would
        //   refer to offsets in the *compressed* stream, even though we discard
        //   it and decompress it on the fly, so we don't know such offsets.
    }

    AVS_LIST(anjay_http_download_ctx_t) ctx =
            AVS_LIST_NEW_ELEMENT(anjay_http_download_ctx_t);
    if (!ctx) {
        dl_log(ERROR, "out of memory");
        return -ENOMEM;
    }

    static const anjay_download_ctx_vtable_t VTABLE = {
        .get_socket = get_http_socket,
        .handle_packet = handle_http_packet,
        .cleanup = cleanup_http_transfer
    };
    ctx->common.vtable = &VTABLE;

    int result = 0;
    if (!(ctx->client = avs_http_new(&AVS_HTTP_DEFAULT_BUFFER_SIZES))) {
        result = -ENOMEM;
        goto error;
    }
    ctx->ssl_configuration.security = cfg->security_info;
    avs_http_ssl_configuration(ctx->client, &ctx->ssl_configuration);

    if (!(ctx->parsed_url = avs_url_parse(cfg->url))) {
        result = -EINVAL;
        goto error;
    }

    ctx->common.id = id;
    ctx->common.on_next_block = cfg->on_next_block;
    ctx->common.on_download_finished = cfg->on_download_finished;
    ctx->common.user_data = cfg->user_data;

    if (_anjay_sched_now(_anjay_downloader_get_anjay(dl)->sched,
                         &ctx->send_request_job, send_request,
                         (void *) ctx->common.id)) {
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
