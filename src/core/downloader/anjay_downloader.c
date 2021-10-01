/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#ifdef ANJAY_WITH_DOWNLOADER

#    include <inttypes.h>

#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_memory.h>
#    include <avsystem/commons/avs_utils.h>

#    include "../anjay_core.h"
#    include "../anjay_downloader.h"

#    define ANJAY_DOWNLOADER_INTERNALS

#    include "anjay_private.h"

#    define INVALID_DOWNLOAD_ID ((uintptr_t) NULL)

VISIBILITY_SOURCE_BEGIN

struct anjay_download_ctx {
    anjay_download_ctx_common_t common;
};

int _anjay_downloader_init(anjay_downloader_t *dl, anjay_unlocked_t *anjay) {
    assert(anjay);
    assert(_anjay_downloader_get_anjay(dl) == anjay);
    assert(_anjay_downloader_get_anjay(dl)->sched);

    if (!anjay || anjay != _anjay_downloader_get_anjay(dl) || !anjay->sched) {
        dl_log(ERROR, _("invalid anjay pointer passed"));
        return -1;
    }

    *dl = (anjay_downloader_t) {
        .next_id = 1,
        .downloads = NULL,
    };
    return 0;
}

static void cleanup_transfer(AVS_LIST(anjay_download_ctx_t) *ctx) {
    assert(ctx);
    assert(*ctx);
    assert((*ctx)->common.vtable);

    (*ctx)->common.vtable->cleanup(ctx);
}

avs_error_t
_anjay_downloader_call_on_next_block(anjay_download_ctx_common_t *ctx,
                                     const uint8_t *data,
                                     size_t data_size,
                                     const anjay_etag_t *etag) {
    anjay_unlocked_t *anjay = _anjay_downloader_get_anjay(ctx->dl);
    anjay_download_next_block_handler_t *handler = ctx->on_next_block;
    void *user_data = ctx->user_data;
    assert(handler);

    avs_error_t err = avs_errno(AVS_EINVAL);
    (void) err;
    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    err = handler(anjay_locked, data, data_size, etag, user_data);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
    return err;
}

static void call_on_download_finished(anjay_download_ctx_t *ctx,
                                      anjay_download_status_t status) {
    anjay_unlocked_t *anjay = _anjay_downloader_get_anjay(ctx->common.dl);
    anjay_download_finished_handler_t *handler =
            ctx->common.on_download_finished;
    void *user_data = ctx->common.user_data;
    assert(handler);

    ANJAY_MUTEX_UNLOCK_FOR_CALLBACK(anjay_locked, anjay);
    handler(anjay_locked, status, user_data);
    ANJAY_MUTEX_LOCK_AFTER_CALLBACK(anjay_locked);
}

void _anjay_downloader_abort_transfer(AVS_LIST(anjay_download_ctx_t) *ctx,
                                      anjay_download_status_t status) {
    assert(ctx);
    assert(*ctx);

    switch (status.result) {
    case ANJAY_DOWNLOAD_FINISHED:
        dl_log(TRACE,
               _("aborting download id = ") "%" PRIuPTR _(
                       ": finished successfully"),
               (*ctx)->common.id);
        break;
    case ANJAY_DOWNLOAD_ERR_FAILED:
        dl_log(TRACE,
               _("aborting download id = ") "%" PRIuPTR _(
                       ": failed, error: ") "%s",
               (*ctx)->common.id, AVS_COAP_STRERROR(status.details.error));
        break;
    case ANJAY_DOWNLOAD_ERR_INVALID_RESPONSE:
        dl_log(TRACE,
               _("aborting download id = ") "%" PRIuPTR _(
                       ": invalid response, status code: ") "%d",
               (*ctx)->common.id, status.details.status_code);
        break;
    case ANJAY_DOWNLOAD_ERR_EXPIRED:
        dl_log(TRACE, _("aborting download id = ") "%" PRIuPTR _(": expired"),
               (*ctx)->common.id);
        break;
    case ANJAY_DOWNLOAD_ERR_ABORTED:
        dl_log(TRACE, _("aborting download id = ") "%" PRIuPTR _(": aborted"),
               (*ctx)->common.id);
        break;
    }

    call_on_download_finished(*ctx, status);

    avs_sched_del(&(*ctx)->common.reconnect_job_handle);
    cleanup_transfer(ctx);
}

static void suspend_transfer(anjay_download_ctx_t *ctx) {
    assert(ctx);
    assert(ctx->common.vtable);
    ctx->common.vtable->suspend(ctx);
}

static void reconnect_transfer(AVS_LIST(anjay_download_ctx_t) *ctx) {
    assert(ctx);
    assert(*ctx);
    assert((*ctx)->common.vtable);

    avs_error_t err = (*ctx)->common.vtable->reconnect(ctx);
    if (avs_is_err(err)) {
        _anjay_downloader_abort_transfer(ctx,
                                         _anjay_download_status_failed(err));
    }
}

void _anjay_downloader_cleanup(anjay_downloader_t *dl) {
    assert(dl);
    while (dl->downloads) {
        _anjay_downloader_abort_transfer(&dl->downloads,
                                         _anjay_download_status_aborted());
    }
}

static avs_net_socket_t *get_ctx_socket(anjay_download_ctx_t *ctx) {
    assert(ctx);
    assert(ctx->common.vtable);
    return ctx->common.vtable->get_socket(ctx);
}

static anjay_socket_transport_t
get_ctx_socket_transport(anjay_download_ctx_t *ctx) {
    assert(ctx);
    assert(ctx->common.vtable);
    return ctx->common.vtable->get_socket_transport(ctx);
}

static AVS_LIST(anjay_download_ctx_t) *
find_ctx_ptr_by_socket(anjay_downloader_t *dl, avs_net_socket_t *socket) {
    assert(socket);
    AVS_LIST(anjay_download_ctx_t) *ctx;
    AVS_LIST_FOREACH_PTR(ctx, &dl->downloads) {
        if (get_ctx_socket(*ctx) == socket) {
            return ctx;
        }
    }
    return NULL;
}

int _anjay_downloader_get_sockets(anjay_downloader_t *dl,
                                  AVS_LIST(anjay_socket_entry_t) *out_socks) {
    AVS_LIST(anjay_socket_entry_t) sockets = NULL;
    AVS_LIST(anjay_download_ctx_t) dl_ctx;

    AVS_LIST_FOREACH(dl_ctx, dl->downloads) {
        avs_net_socket_t *socket = get_ctx_socket(dl_ctx);
        if (_anjay_socket_is_online(socket)) {
            AVS_LIST(anjay_socket_entry_t) elem =
                    AVS_LIST_NEW_ELEMENT(anjay_socket_entry_t);
            if (!elem) {
                AVS_LIST_CLEAR(&sockets);
                return -1;
            }

            elem->socket = socket;
            elem->transport = get_ctx_socket_transport(dl_ctx);
            elem->ssid = ANJAY_SSID_ANY;
            elem->queue_mode = false;
            AVS_LIST_INSERT(&sockets, elem);
        }
    }

    AVS_LIST_INSERT(out_socks, sockets);
    return 0;
}

AVS_LIST(anjay_download_ctx_t) *
_anjay_downloader_find_ctx_ptr_by_id(anjay_downloader_t *dl, uintptr_t id) {
    AVS_LIST(anjay_download_ctx_t) *ctx;
    AVS_LIST_FOREACH_PTR(ctx, &dl->downloads) {
        if ((*ctx)->common.id == id) {
            return ctx;
        }
    }

    return NULL;
}

int _anjay_downloader_handle_packet(anjay_downloader_t *dl,
                                    avs_net_socket_t *socket) {
    assert(&_anjay_downloader_get_anjay(dl)->downloader == dl);

    AVS_LIST(anjay_download_ctx_t) *ctx = find_ctx_ptr_by_socket(dl, socket);
    if (!ctx) {
        // unknown socket
        return -1;
    }

    assert(*ctx);
    assert((*ctx)->common.vtable);
    (*ctx)->common.vtable->handle_packet(ctx);
    return 0;
}

#    if defined(ANJAY_WITH_HTTP_DOWNLOAD) || defined(ANJAY_WITH_COAP_DOWNLOAD)
static uintptr_t find_free_id(anjay_downloader_t *dl) {
    uintptr_t id;

    // One could think this can loop forever if all download IDs are in use.
    // However, uintptr_t is an integer as large as a pointer, and a normal
    // pointer needs to be able to address every byte that may be allocated
    // by avs_malloc(). Since we use more than one byte per download object,
    // we can safely assume we will run out of RAM before running out
    // of download IDs.
    do {
        id = dl->next_id++;
    } while (id == INVALID_DOWNLOAD_ID
             || _anjay_downloader_find_ctx_ptr_by_id(dl, id) != NULL);

    return id;
}
#    endif // ANJAY_WITH_HTTP_DOWNLOAD || ANJAY_WITH_COAP_DOWNLOAD

#    ifdef ANJAY_WITH_HTTP_DOWNLOAD
static bool starts_with(const char *haystack, const char *needle) {
    return avs_strncasecmp(haystack, needle, strlen(needle)) == 0;
}
#    endif // ANJAY_WITH_HTTP_DOWNLOAD

static avs_error_t find_downloader_ctx_constructor(
        const char *url,
        anjay_downloader_ctx_constructor_t **out_constructor,
        anjay_socket_transport_t *out_transport) {
#    ifdef ANJAY_WITH_COAP_DOWNLOAD
    const anjay_transport_info_t *transport_info =
            _anjay_transport_info_by_uri_scheme(url);
    if (transport_info != NULL) {
        *out_constructor = _anjay_downloader_coap_ctx_new;
        *out_transport = transport_info->transport;
        return AVS_OK;
    }
#    endif // ANJAY_WITH_COAP_DOWNLOAD
#    ifdef ANJAY_WITH_HTTP_DOWNLOAD
    if (starts_with(url, "http")) {
        *out_constructor = _anjay_downloader_http_ctx_new;
        *out_transport = ANJAY_SOCKET_TRANSPORT_TCP;
        return AVS_OK;
    }
#    endif // ANJAY_WITH_HTTP_DOWNLOAD
    dl_log(WARNING, _("unrecognized protocol in URL: ") "%s", url);
    *out_constructor = NULL;
    return avs_errno(AVS_EPROTONOSUPPORT);
}

avs_error_t _anjay_downloader_download(anjay_downloader_t *dl,
                                       anjay_download_handle_t *out_handle,
                                       const anjay_download_config_t *config) {
    assert(&_anjay_downloader_get_anjay(dl)->downloader == dl);
    assert(out_handle);

    anjay_downloader_ctx_constructor_t *constructor = NULL;
    anjay_socket_transport_t transport;
    avs_error_t err = find_downloader_ctx_constructor(config->url, &constructor,
                                                      &transport);

    AVS_LIST(anjay_download_ctx_t) dl_ctx = NULL;
    if (avs_is_ok(err)) {
        assert(constructor);
        if (_anjay_socket_transport_included(
                    _anjay_downloader_get_anjay(dl)->online_transports,
                    transport)) {
            err = constructor(dl, &dl_ctx, config, find_free_id(dl));
        } else {
            dl_log(WARNING, _("transport currently offline for URL: ") "%s",
                   config->url);
            err = avs_errno(AVS_ENODEV);
        }
    }

    if (dl_ctx) {
        AVS_LIST_APPEND(&dl->downloads, dl_ctx);

        assert(dl_ctx->common.id != INVALID_DOWNLOAD_ID);
        dl_log(INFO, _("download scheduled: ") "%s", config->url);
        *out_handle = (anjay_download_handle_t) dl_ctx->common.id;
        return AVS_OK;
    } else {
        assert(avs_is_err(err));
        return err;
    }
}

avs_error_t
_anjay_downloader_set_next_block_offset(anjay_downloader_t *dl,
                                        anjay_download_handle_t handle,
                                        size_t next_block_offset) {
    uintptr_t id = (uintptr_t) handle;

    AVS_LIST(anjay_download_ctx_t) *ctx =
            _anjay_downloader_find_ctx_ptr_by_id(dl, id);
    if (!ctx) {
        dl_log(DEBUG, _("download id = ") "%" PRIuPTR _(" not found"), id);
        return avs_errno(AVS_ENOENT);
    }
    return (*ctx)->common.vtable->set_next_block_offset(*ctx,
                                                        next_block_offset);
}

void _anjay_downloader_abort(anjay_downloader_t *dl,
                             anjay_download_handle_t handle) {
    uintptr_t id = (uintptr_t) handle;

    AVS_LIST(anjay_download_ctx_t) *ctx =
            _anjay_downloader_find_ctx_ptr_by_id(dl, id);
    if (!ctx) {
        dl_log(DEBUG,
               _("download id = ") "%" PRIuPTR _(" not found (expired?)"), id);
    } else {
        _anjay_downloader_abort_transfer(ctx, _anjay_download_status_aborted());
    }
}

static void reconnect_job(avs_sched_t *sched, const void *id_ptr) {
    anjay_t *anjay_locked = _anjay_get_from_sched(sched);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    uintptr_t id = *(const uintptr_t *) id_ptr;
    AVS_LIST(anjay_download_ctx_t) *ctx_ptr =
            _anjay_downloader_find_ctx_ptr_by_id(&anjay->downloader, id);
    if (!ctx_ptr) {
        dl_log(DEBUG,
               _("download id = ") "%" PRIuPTR _(" not found (expired?)"), id);
    } else if (_anjay_socket_transport_included(anjay->online_transports,
                                                get_ctx_socket_transport(
                                                        *ctx_ptr))) {
        reconnect_transfer(ctx_ptr);
    } else {
        suspend_transfer(*ctx_ptr);
    }
    ANJAY_MUTEX_UNLOCK(anjay_locked);
}

static int schedule_reconnect(anjay_download_ctx_t *ctx) {
    return AVS_SCHED_NOW(_anjay_downloader_get_anjay(ctx->common.dl)->sched,
                         &ctx->common.reconnect_job_handle, reconnect_job,
                         &ctx->common.id, sizeof(ctx->common.id));
}

int _anjay_downloader_sched_reconnect(anjay_downloader_t *dl,
                                      anjay_transport_set_t transport_set) {
    int result = 0;
    AVS_LIST(anjay_download_ctx_t) ctx;
    AVS_LIST_FOREACH(ctx, dl->downloads) {
        if (!ctx->common.reconnect_job_handle
                && _anjay_socket_transport_included(
                           transport_set, get_ctx_socket_transport(ctx))) {
            int partial_result = schedule_reconnect(ctx);
            if (!result && partial_result) {
                result = partial_result;
            }
        }
    }
    return result;
}

int _anjay_downloader_sync_online_transports(anjay_downloader_t *dl) {
    int result = 0;
    AVS_LIST(anjay_download_ctx_t) ctx;
    AVS_LIST_FOREACH(ctx, dl->downloads) {
        if (!ctx->common.reconnect_job_handle
                && _anjay_socket_transport_included(
                           _anjay_downloader_get_anjay(dl)->online_transports,
                           get_ctx_socket_transport(ctx))
                               != _anjay_socket_is_online(
                                          get_ctx_socket(ctx))) {
            int partial_result = schedule_reconnect(ctx);
            if (!result && partial_result) {
                result = partial_result;
            }
        }
    }
    return result;
}

#endif // ANJAY_WITH_DOWNLOADER
