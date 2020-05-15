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

int _anjay_downloader_init(anjay_downloader_t *dl, anjay_t *anjay) {
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

void _anjay_downloader_abort_transfer(anjay_downloader_t *dl,
                                      AVS_LIST(anjay_download_ctx_t) *ctx,
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

    (*ctx)->common.on_download_finished(_anjay_downloader_get_anjay(dl), status,
                                        (*ctx)->common.user_data);

    cleanup_transfer(ctx);
}

static void reconnect_transfer(anjay_downloader_t *dl,
                               AVS_LIST(anjay_download_ctx_t) *ctx) {
    assert(ctx);
    assert(*ctx);
    assert((*ctx)->common.vtable);

    avs_error_t err = (*ctx)->common.vtable->reconnect(dl, ctx);
    if (avs_is_err(err)) {
        _anjay_downloader_abort_transfer(dl, ctx,
                                         _anjay_download_status_failed(err));
    }
}

void _anjay_downloader_cleanup(anjay_downloader_t *dl) {
    assert(dl);
    avs_sched_del(&dl->reconnect_job_handle);
    while (dl->downloads) {
        _anjay_downloader_abort_transfer(dl, &dl->downloads,
                                         _anjay_download_status_aborted());
    }
}

static int get_ctx_socket(anjay_downloader_t *dl,
                          anjay_download_ctx_t *ctx,
                          avs_net_socket_t **out_socket,
                          anjay_socket_transport_t *out_transport) {
    assert(dl);
    assert(ctx);
    assert(ctx->common.vtable);
    int result =
            ctx->common.vtable->get_socket(dl, ctx, out_socket, out_transport);
    if (!result) {
        assert(*out_socket);
    }
    return result;
}

static AVS_LIST(anjay_download_ctx_t) *
find_ctx_ptr_by_socket(anjay_downloader_t *dl, avs_net_socket_t *socket) {
    AVS_LIST(anjay_download_ctx_t) *ctx;
    AVS_LIST_FOREACH_PTR(ctx, &dl->downloads) {
        avs_net_socket_t *ctx_socket = NULL;
        if (!get_ctx_socket(dl, *ctx, &ctx_socket,
                            &(anjay_socket_transport_t) {
                                    (anjay_socket_transport_t) 0 })
                && ctx_socket == socket) {
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
        avs_net_socket_t *socket = NULL;
        anjay_socket_transport_t transport;
        if (!get_ctx_socket(dl, dl_ctx, &socket, &transport)) {
            AVS_LIST(anjay_socket_entry_t) elem =
                    AVS_LIST_NEW_ELEMENT(anjay_socket_entry_t);
            if (!elem) {
                AVS_LIST_CLEAR(&sockets);
                return -1;
            }

            elem->socket = socket;
            elem->transport = transport;
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
    (*ctx)->common.vtable->handle_packet(dl, ctx);
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

avs_error_t _anjay_downloader_download(anjay_downloader_t *dl,
                                       anjay_download_handle_t *out_handle,
                                       const anjay_download_config_t *config) {
    assert(&_anjay_downloader_get_anjay(dl)->downloader == dl);
    assert(out_handle);

    AVS_LIST(anjay_download_ctx_t) dl_ctx = NULL;
    avs_error_t err = avs_errno(AVS_EPROTONOSUPPORT);
#    ifdef ANJAY_WITH_COAP_DOWNLOAD
    if (_anjay_transport_info_by_uri_scheme(config->url) != NULL) {
        err = _anjay_downloader_coap_ctx_new(dl, &dl_ctx, config,
                                             find_free_id(dl));
    } else
#    endif // ANJAY_WITH_COAP_DOWNLOAD
#    ifdef ANJAY_WITH_HTTP_DOWNLOAD
            if (starts_with(config->url, "http")) {
        err = _anjay_downloader_http_ctx_new(dl, &dl_ctx, config,
                                             find_free_id(dl));
    } else
#    endif // ANJAY_WITH_HTTP_DOWNLOAD
    {
        dl_log(WARNING, _("unrecognized protocol in URL: ") "%s", config->url);
    }

    if (dl_ctx) {
        AVS_LIST_APPEND(&dl->downloads, dl_ctx);

        assert(dl_ctx->common.id != INVALID_DOWNLOAD_ID);
        dl_log(INFO, _("download scheduled: ") "%s", config->url);
        *out_handle = (anjay_download_handle_t) dl_ctx->common.id;
        err = AVS_OK;
    }
    return err;
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
        _anjay_downloader_abort_transfer(dl, ctx,
                                         _anjay_download_status_aborted());
    }
}

static void reconnect_all_job(avs_sched_t *sched, const void *dummy) {
    (void) dummy;
    anjay_t *anjay = _anjay_get_from_sched(sched);
    AVS_LIST(anjay_download_ctx_t) *ctx_ptr;
    AVS_LIST(anjay_download_ctx_t) helper;
    AVS_LIST_DELETABLE_FOREACH_PTR(ctx_ptr, helper,
                                   &anjay->downloader.downloads) {
        reconnect_transfer(&anjay->downloader, ctx_ptr);
    }
}

int _anjay_downloader_sched_reconnect_all(anjay_downloader_t *dl) {
    if (dl->reconnect_job_handle) {
        dl_log(DEBUG, _("reconnect already scheduled, ignoring"));
        return 0;
    }
    return AVS_SCHED_NOW(_anjay_downloader_get_anjay(dl)->sched,
                         &dl->reconnect_job_handle, reconnect_all_job, NULL, 0);
}

#endif // ANJAY_WITH_DOWNLOADER
