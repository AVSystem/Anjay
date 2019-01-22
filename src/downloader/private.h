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

#ifndef ANJAY_DOWNLOADER_PRIVATE_H
#define ANJAY_DOWNLOADER_PRIVATE_H

#include "../anjay_core.h"
#include "../downloader.h"

#ifndef ANJAY_DOWNLOADER_INTERNALS
#    error "downloader/private.h is not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

#define dl_log(...) _anjay_log(downloader, __VA_ARGS__)

typedef struct {
    int (*get_socket)(anjay_downloader_t *dl,
                      anjay_download_ctx_t *ctx,
                      avs_net_abstract_socket_t **out_socket,
                      anjay_socket_transport_t *out_transport);
    void (*handle_packet)(anjay_downloader_t *dl,
                          AVS_LIST(anjay_download_ctx_t) *ctx_ptr);
    void (*cleanup)(anjay_downloader_t *dl,
                    AVS_LIST(anjay_download_ctx_t) *ctx_ptr);
    int (*reconnect)(anjay_downloader_t *dl,
                     AVS_LIST(anjay_download_ctx_t) *ctx_ptr);
} anjay_download_ctx_vtable_t;

typedef struct {
    const anjay_download_ctx_vtable_t *vtable;

    uintptr_t id;

    anjay_download_next_block_handler_t *on_next_block;
    anjay_download_finished_handler_t *on_download_finished;
    void *user_data;
} anjay_download_ctx_common_t;

static inline anjay_t *_anjay_downloader_get_anjay(anjay_downloader_t *dl) {
    return AVS_CONTAINER_OF(dl, anjay_t, downloader);
}

AVS_LIST(anjay_download_ctx_t) *
_anjay_downloader_find_ctx_ptr_by_id(anjay_downloader_t *dl, uintptr_t id);

void _anjay_downloader_abort_transfer(anjay_downloader_t *dl,
                                      AVS_LIST(anjay_download_ctx_t) *ctx,
                                      int result,
                                      int errno_value);

#ifdef WITH_BLOCK_DOWNLOAD
int _anjay_downloader_coap_ctx_new(anjay_downloader_t *dl,
                                   AVS_LIST(anjay_download_ctx_t) *out_dl_ctx,
                                   const anjay_download_config_t *cfg,
                                   uintptr_t id);
#endif // WITH_BLOCK_DOWNLOAD

#ifdef WITH_HTTP_DOWNLOAD
int _anjay_downloader_http_ctx_new(anjay_downloader_t *dl,
                                   AVS_LIST(anjay_download_ctx_t) *out_dl_ctx,
                                   const anjay_download_config_t *cfg,
                                   uintptr_t id);
#endif // WITH_HTTP_DOWNLOAD

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_DOWNLOADER_PRIVATE_H */
