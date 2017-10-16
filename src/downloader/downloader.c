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

#include <avsystem/commons/utils.h>

#include "../anjay_core.h"
#include "../downloader.h"

#define ANJAY_DOWNLOADER_INTERNALS

#include "private.h"

#define INVALID_DOWNLOAD_ID ((uintptr_t) NULL)

VISIBILITY_SOURCE_BEGIN

struct anjay_download_ctx {
    anjay_download_ctx_common_t common;
};

int _anjay_downloader_init(anjay_downloader_t *dl,
                           anjay_t *anjay,
                           coap_id_source_t **id_source_move) {
    assert(anjay);
    assert(_anjay_downloader_get_anjay(dl) == anjay);
    assert(_anjay_downloader_get_anjay(dl)->sched);
    assert(_anjay_downloader_get_anjay(dl)->coap_ctx);

    if (!anjay || anjay != _anjay_downloader_get_anjay(dl) || !anjay->sched
            || !anjay->coap_ctx) {
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
    assert((*ctx)->common.vtable);

    (*ctx)->common.vtable->cleanup(dl, ctx);
}

void _anjay_downloader_abort_transfer(anjay_downloader_t *dl,
                                      AVS_LIST(anjay_download_ctx_t) *ctx,
                                      int result) {
    assert(ctx);
    assert(*ctx);

    dl_log(TRACE, "aborting download id = %" PRIuPTR ", result = %d",
           (*ctx)->common.id, result);

    (*ctx)->common.on_download_finished(_anjay_downloader_get_anjay(dl), result,
                                        (*ctx)->common.user_data);

    cleanup_transfer(dl, ctx);
}

void _anjay_downloader_cleanup(anjay_downloader_t *dl) {
    assert(dl);
    while (dl->downloads) {
        _anjay_downloader_abort_transfer(dl, &dl->downloads,
                                         ANJAY_DOWNLOAD_ERR_ABORTED);
    }

    _anjay_coap_id_source_release(&dl->id_source);
}

static avs_net_abstract_socket_t *get_ctx_socket(anjay_downloader_t *dl,
                                                 anjay_download_ctx_t *ctx) {
    assert(dl);
    assert(ctx);
    assert(ctx->common.vtable);
    return ctx->common.vtable->get_socket(dl, ctx);
}

static AVS_LIST(anjay_download_ctx_t) *
find_ctx_ptr_by_socket(anjay_downloader_t *dl,
                       avs_net_abstract_socket_t *socket) {
    AVS_LIST(anjay_download_ctx_t) *ctx;
    AVS_LIST_FOREACH_PTR(ctx, &dl->downloads) {
        if (get_ctx_socket(dl, *ctx) == socket) {
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

        *elem = get_ctx_socket(dl, dl_ctx);
        AVS_LIST_INSERT(&sockets, elem);
    }

    AVS_LIST_INSERT(out_socks, sockets);
    return 0;
}

AVS_LIST(anjay_download_ctx_t) *
_anjay_downloader_find_ctx_ptr_by_id(anjay_downloader_t *dl,
                                     uintptr_t id) {
    AVS_LIST(anjay_download_ctx_t) *ctx;
    AVS_LIST_FOREACH_PTR(ctx, &dl->downloads) {
        if ((*ctx)->common.id == id) {
            return ctx;
        }
    }

    return NULL;
}

int _anjay_downloader_handle_packet(anjay_downloader_t *dl,
                                    avs_net_abstract_socket_t *socket) {
    assert(&_anjay_downloader_get_anjay(dl)->downloader == dl);

    AVS_LIST(anjay_download_ctx_t) *ctx =
            find_ctx_ptr_by_socket(dl, socket);
    if (!ctx) {
        dl_log(DEBUG, "unknown socket");
        return -1;
    }

    assert(*ctx);
    assert((*ctx)->common.vtable);
    (*ctx)->common.vtable->handle_packet(dl, ctx);
    return 0;
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
    } while (id == INVALID_DOWNLOAD_ID
            || _anjay_downloader_find_ctx_ptr_by_id(dl, id) != NULL);

    return id;
}

static bool starts_with(const char *haystack, const char *needle) {
    return avs_strncasecmp(haystack, needle, strlen(needle)) == 0;
}

anjay_download_handle_t
_anjay_downloader_download(anjay_downloader_t *dl,
                           const anjay_download_config_t *config) {
    assert(&_anjay_downloader_get_anjay(dl)->downloader == dl);

    AVS_LIST(anjay_download_ctx_t) dl_ctx = NULL;
#ifdef WITH_BLOCK_DOWNLOAD
    if (starts_with(config->url, "coap")) {
        dl_ctx = _anjay_downloader_coap_ctx_new(dl, config, find_free_id(dl));
    } else
#endif // WITH_BLOCK_DOWNLOAD
#ifdef WITH_HTTP_DOWNLOAD
    if (starts_with(config->url, "http")) {
        dl_ctx = _anjay_downloader_http_ctx_new(dl, config, find_free_id(dl));
    } else
#endif // WITH_HTTP_DOWNLOAD
    {
        dl_log(ERROR, "unrecognized protocol in URL: %s", config->url);
    }
    if (!dl_ctx) {
        return (anjay_download_handle_t) INVALID_DOWNLOAD_ID;
    }

    AVS_LIST_INSERT(&dl->downloads, dl_ctx);

    assert(dl_ctx->common.id != INVALID_DOWNLOAD_ID);
    dl_log(INFO, "download scheduled: %s", config->url);
    return (anjay_download_handle_t) dl_ctx->common.id;
}

void _anjay_downloader_abort(anjay_downloader_t *dl,
                             anjay_download_handle_t handle) {
    uintptr_t id = (uintptr_t)handle;

    AVS_LIST(anjay_download_ctx_t) *ctx =
            _anjay_downloader_find_ctx_ptr_by_id(dl, id);
    if (!ctx) {
        dl_log(DEBUG, "download id = %" PRIuPTR " not found (expired?)", id);
    } else {
        _anjay_downloader_abort_transfer(dl, ctx, ANJAY_DOWNLOAD_ERR_ABORTED);
    }
}
