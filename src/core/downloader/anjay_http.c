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

#    ifndef ANJAY_WITH_DOWNLOADER
#        error "ANJAY_WITH_HTTP_DOWNLOAD requires ANJAY_WITH_DOWNLOADER to be enabled"
#    endif // ANJAY_WITH_DOWNLOADER

#    include <errno.h>
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
    anjay_security_config_cache_t security_config_cache;
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

static int parse_number(const char **inout_ptr, unsigned long long *out_value) {
    assert(inout_ptr);
    if (**inout_ptr == '-') {
        return -1;
    }
    char *endptr;
    *out_value = strtoull(*inout_ptr, &endptr, 10);
    if (endptr == *inout_ptr || (*out_value == ULLONG_MAX && errno == ERANGE)) {
        return -1;
    }
    *inout_ptr = endptr;
    return 0;
}

static int read_start_byte_from_content_range(const char *content_range,
                                              uint64_t *out_start_byte) {
    unsigned long long complete_length;
    unsigned long long start;
    unsigned long long end;
    if (avs_match_token(&content_range, "bytes", AVS_SPACES)
            || parse_number(&content_range, &start) || *content_range++ != '-'
            || parse_number(&content_range, &end) || *content_range++ != '/'
            || *content_range == '\0') {
        return -1;
    }

    *out_start_byte = start;

    return (strcmp(content_range, "*") == 0
            || (*content_range != '-'
                && !_anjay_safe_strtoull(content_range, &complete_length)
                && complete_length >= 1 && complete_length - 1 == end))
                   ? 0
                   : -1;
}

static anjay_etag_t *read_etag(const char *text) {
    size_t len = strlen(text);
    if (len < 2 || len > UINT8_MAX + 2 || text[0] != '"'
            || text[len - 1] != '"') {
        return NULL;
    }
    anjay_etag_t *result = anjay_etag_new((uint8_t) (len - 2));
    if (result) {
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
            ctx->bytes_downloaded += bytes_read;
            while (ctx->bytes_downloaded > ctx->bytes_written) {
                size_t bytes_to_write =
                        ctx->bytes_downloaded - ctx->bytes_written;
                assert(bytes_read >= bytes_to_write);
                size_t original_offset = ctx->bytes_written;
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
                if (ctx->bytes_written == original_offset) {
                    ctx->bytes_written += bytes_to_write;
                }
            }
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

    ctx->bytes_downloaded = 0;

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

static avs_net_socket_t *get_http_socket(anjay_downloader_t *dl,
                                         anjay_download_ctx_t *ctx) {
    (void) dl;
    return avs_stream_net_getsock(((anjay_http_download_ctx_t *) ctx)->stream);
}

static anjay_socket_transport_t
get_http_socket_transport(anjay_downloader_t *dl, anjay_download_ctx_t *ctx) {
    (void) dl;
    (void) ctx;
    return ANJAY_SOCKET_TRANSPORT_TCP;
}

static void cleanup_http_transfer(AVS_LIST(anjay_download_ctx_t) *ctx_ptr) {
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) *ctx_ptr;
    avs_sched_del(&ctx->next_action_job);
    avs_free(ctx->etag);
    avs_stream_cleanup(&ctx->stream);
    avs_url_free(ctx->parsed_url);
    avs_http_free(ctx->client);
    _anjay_security_config_cache_cleanup(&ctx->security_config_cache);
    AVS_LIST_DELETE(ctx_ptr);
}

static void suspend_http_transfer(anjay_downloader_t *dl,
                                  anjay_download_ctx_t *ctx_) {
    (void) dl;
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) ctx_;
    avs_sched_del(&ctx->next_action_job);
    avs_stream_cleanup(&ctx->stream);
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

static avs_error_t set_next_http_block_offset(anjay_downloader_t *dl,
                                              anjay_download_ctx_t *ctx_,
                                              size_t next_block_offset) {
    (void) dl;
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) ctx_;
    if (next_block_offset <= ctx->bytes_written) {
        dl_log(DEBUG, _("attempted to move download offset backwards"));
        return avs_errno(AVS_EINVAL);
    }
    ctx->bytes_written = next_block_offset;
    return AVS_OK;
}

static avs_error_t copy_psk_info(avs_net_psk_info_t *dest,
                                 const avs_net_psk_info_t *src,
                                 anjay_security_config_cache_t *cache) {
    *dest = *src;
    size_t psk_buffer_size = 0;
    if (src->identity) {
        psk_buffer_size += src->identity_size;
    }
    if (src->psk) {
        psk_buffer_size += src->psk_size;
    }
    assert(!cache->psk_buffer);
    if (!(cache->psk_buffer = avs_malloc(psk_buffer_size))) {
        dl_log(ERROR, _("Out of memory"));
        return avs_errno(AVS_ENOMEM);
    }
    char *psk_buffer_ptr = (char *) cache->psk_buffer;
    if (src->identity) {
        memcpy(psk_buffer_ptr, src->identity, src->identity_size);
        dest->identity = psk_buffer_ptr;
        psk_buffer_ptr += src->identity_size;
    }
    if (src->psk) {
        memcpy(psk_buffer_ptr, src->psk, src->psk_size);
        dest->psk = psk_buffer_ptr;
    }
    return AVS_OK;
}

static avs_error_t
copy_certificate_chain(avs_crypto_certificate_chain_info_t *dest,
                       const avs_crypto_certificate_chain_info_t *src,
                       avs_crypto_certificate_chain_info_t **cache_ptr) {
    if (src->desc.source == AVS_CRYPTO_DATA_SOURCE_EMPTY) {
        *dest = *src;
        return AVS_OK;
    }
    size_t element_count;
    avs_error_t err = avs_crypto_certificate_chain_info_copy_as_array(
            cache_ptr, &element_count, *src);
    if (avs_is_err(err)) {
        return err;
    }
    *dest = avs_crypto_certificate_chain_info_from_array(*cache_ptr,
                                                         element_count);
    return AVS_OK;
}

static avs_error_t copy_cert_info(avs_net_certificate_info_t *dest,
                                  const avs_net_certificate_info_t *src,
                                  anjay_security_config_cache_t *cache) {
    *dest = *src;
    avs_error_t err;
    if (avs_is_err((err = copy_certificate_chain(&dest->trusted_certs,
                                                 &src->trusted_certs,
                                                 &cache->trusted_certs_array)))
            || avs_is_err((
                       err = copy_certificate_chain(&dest->client_cert,
                                                    &src->client_cert,
                                                    &cache->client_cert_array)))
            || avs_is_err((err = avs_crypto_private_key_info_copy(
                                   &cache->client_key, src->client_key)))) {
        return err;
    }
    dest->client_key = *cache->client_key;
    if (src->cert_revocation_lists.desc.source
            == AVS_CRYPTO_DATA_SOURCE_EMPTY) {
        dest->cert_revocation_lists = src->cert_revocation_lists;
    } else {
        size_t element_count;
        if (avs_is_err(
                    (err = avs_crypto_cert_revocation_list_info_copy_as_array(
                             &cache->cert_revocation_lists_array,
                             &element_count, src->cert_revocation_lists)))) {
            return err;
        }
        dest->cert_revocation_lists =
                avs_crypto_cert_revocation_list_info_from_array(
                        cache->cert_revocation_lists_array, element_count);
    }
    return AVS_OK;
}

static avs_error_t copy_security_info(avs_net_security_info_t *dest,
                                      const avs_net_security_info_t *src,
                                      anjay_security_config_cache_t *cache) {
    dest->mode = src->mode;
    switch (src->mode) {
    case AVS_NET_SECURITY_PSK:
        return copy_psk_info(&dest->data.psk, &src->data.psk, cache);
    case AVS_NET_SECURITY_CERTIFICATE:
        return copy_cert_info(&dest->data.cert, &src->data.cert, cache);
    default:
        dl_log(ERROR, _("Invalid security mode: ") "%d", (int) src->mode);
        return avs_errno(AVS_EINVAL);
    }
}

static avs_error_t http_ssl_pre_connect_cb(avs_http_t *http,
                                           avs_net_socket_t *socket,
                                           const char *hostname,
                                           const char *port,
                                           void *ctx_) {
    (void) http;
    (void) port;
    anjay_http_download_ctx_t *ctx = (anjay_http_download_ctx_t *) ctx_;
    if (!hostname || !ctx->security_config_cache.dane_tlsa_record) {
        return AVS_OK;
    }
    const char *configured_hostname = avs_url_host(ctx->parsed_url);
    if (!configured_hostname || strcmp(configured_hostname, hostname)) {
        // non-original hostname - we're after redirection; do nothing
        return AVS_OK;
    }
    return avs_net_socket_set_opt(
            socket, AVS_NET_SOCKET_OPT_DANE_TLSA_ARRAY,
            (avs_net_socket_opt_value_t) {
                .dane_tlsa_array = {
                    .array_ptr = ctx->security_config_cache.dane_tlsa_record,
                    .array_element_count = 1
                }
            });
}

avs_error_t
_anjay_downloader_http_ctx_new(anjay_downloader_t *dl,
                               AVS_LIST(anjay_download_ctx_t) *out_dl_ctx,
                               const anjay_download_config_t *cfg,
                               uintptr_t id) {

    if (!cfg->on_next_block || !cfg->on_download_finished) {
        dl_log(ERROR, _("invalid download config: handlers not set up"));
        return avs_errno(AVS_EINVAL);
    }

    anjay_t *anjay = _anjay_downloader_get_anjay(dl);
    AVS_LIST(anjay_http_download_ctx_t) ctx =
            AVS_LIST_NEW_ELEMENT(anjay_http_download_ctx_t);
    if (!ctx) {
        dl_log(ERROR, _("out of memory"));
        return avs_errno(AVS_ENOMEM);
    }

    static const anjay_download_ctx_vtable_t VTABLE = {
        .get_socket = get_http_socket,
        .get_socket_transport = get_http_socket_transport,
        .handle_packet = handle_http_packet,
        .cleanup = cleanup_http_transfer,
        .suspend = suspend_http_transfer,
        .reconnect = reconnect_http_transfer,
        .set_next_block_offset = set_next_http_block_offset
    };
    ctx->common.vtable = &VTABLE;

    avs_http_buffer_sizes_t http_buffer_sizes = AVS_HTTP_DEFAULT_BUFFER_SIZES;
    if (cfg->start_offset > 0) {
        // prevent sending Accept-Encoding
        http_buffer_sizes.content_coding_input = 0;
    }

    avs_error_t err = AVS_OK;
    if (!(ctx->client = avs_http_new(&http_buffer_sizes))) {
        err = avs_errno(AVS_ENOMEM);
        goto error;
    }
    if (avs_is_err(
                (err = copy_security_info(&ctx->ssl_configuration.security,
                                          &cfg->security_config.security_info,
                                          &ctx->security_config_cache)))) {
        goto error;
    }
    ctx->ssl_configuration.ciphersuites = anjay->default_tls_ciphersuites;
    if (cfg->security_config.tls_ciphersuites.num_ids) {
        if (_anjay_copy_tls_ciphersuites(
                    &ctx->security_config_cache.ciphersuites,
                    &cfg->security_config.tls_ciphersuites)) {
            err = avs_errno(AVS_ENOMEM);
            goto error;
        }
        ctx->ssl_configuration.ciphersuites =
                ctx->security_config_cache.ciphersuites;
    }
    if (cfg->security_config.dane_tlsa_record
            && !(ctx->security_config_cache.dane_tlsa_record =
                         avs_net_socket_dane_tlsa_array_copy((
                                 const avs_net_socket_dane_tlsa_array_t) {
                             .array_ptr = cfg->security_config.dane_tlsa_record,
                             .array_element_count = 1
                         }))) {
        goto error;
    }
    ctx->ssl_configuration.backend_configuration.preferred_endpoint =
            &ctx->preferred_endpoint;
    ctx->ssl_configuration.prng_ctx = anjay->prng_ctx.ctx;
    avs_http_ssl_configuration(ctx->client, &ctx->ssl_configuration);
    avs_http_ssl_pre_connect_cb(ctx->client, http_ssl_pre_connect_cb, ctx);

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
        if (!(ctx->etag = anjay_etag_clone(cfg->etag))) {
            dl_log(ERROR, _("could not copy ETag"));
            err = avs_errno(AVS_ENOMEM);
            goto error;
        }
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
