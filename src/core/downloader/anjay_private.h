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

#ifndef ANJAY_DOWNLOADER_PRIVATE_H
#define ANJAY_DOWNLOADER_PRIVATE_H

#include "../anjay_core.h"
#include "../anjay_downloader.h"

#ifndef ANJAY_DOWNLOADER_INTERNALS
#    error "downloader/private.h is not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

#define dl_log(...) _anjay_log(downloader, __VA_ARGS__)

typedef struct {
    avs_net_socket_t *(*get_socket)(anjay_download_ctx_t *ctx);
    anjay_socket_transport_t (*get_socket_transport)(anjay_download_ctx_t *ctx);
    void (*handle_packet)(AVS_LIST(anjay_download_ctx_t) *ctx_ptr);
    void (*cleanup)(AVS_LIST(anjay_download_ctx_t) *ctx_ptr);
    void (*suspend)(anjay_download_ctx_t *ctx);
    avs_error_t (*reconnect)(AVS_LIST(anjay_download_ctx_t) *ctx_ptr);
    avs_error_t (*set_next_block_offset)(anjay_download_ctx_t *ctx,
                                         size_t next_block_offset);
} anjay_download_ctx_vtable_t;

typedef struct {
    const anjay_download_ctx_vtable_t *vtable;

    anjay_downloader_t *dl;
    uintptr_t id;
    avs_sched_handle_t reconnect_job_handle;

    anjay_download_next_block_handler_t *on_next_block;
    anjay_download_finished_handler_t *on_download_finished;
    void *user_data;

} anjay_download_ctx_common_t;

static inline anjay_unlocked_t *
_anjay_downloader_get_anjay(anjay_downloader_t *dl) {
    return AVS_CONTAINER_OF(dl, anjay_unlocked_t, downloader);
}

AVS_LIST(anjay_download_ctx_t) *
_anjay_downloader_find_ctx_ptr_by_id(anjay_downloader_t *dl, uintptr_t id);

void _anjay_downloader_abort_transfer(AVS_LIST(anjay_download_ctx_t) *ctx,
                                      anjay_download_status_t status);

avs_error_t
_anjay_downloader_call_on_next_block(anjay_download_ctx_common_t *ctx,
                                     const uint8_t *data,
                                     size_t data_size,
                                     const anjay_etag_t *etag);

static inline anjay_download_status_t _anjay_download_status_success(void) {
    return (anjay_download_status_t) {
        .result = ANJAY_DOWNLOAD_FINISHED
    };
}

static inline anjay_download_status_t
_anjay_download_status_failed(avs_error_t error) {
    return (anjay_download_status_t) {
        .result = ANJAY_DOWNLOAD_ERR_FAILED,
        .details = {
            .error = error
        }
    };
}

static inline anjay_download_status_t
_anjay_download_status_invalid_response(int status_code) {
    return (anjay_download_status_t) {
        .result = ANJAY_DOWNLOAD_ERR_INVALID_RESPONSE,
        .details = {
            .status_code = status_code
        }
    };
}

static inline anjay_download_status_t _anjay_download_status_expired(void) {
    return (anjay_download_status_t) {
        .result = ANJAY_DOWNLOAD_ERR_EXPIRED
    };
}

static inline anjay_download_status_t _anjay_download_status_aborted(void) {
    return (anjay_download_status_t) {
        .result = ANJAY_DOWNLOAD_ERR_ABORTED
    };
}

typedef avs_error_t
anjay_downloader_ctx_constructor_t(anjay_downloader_t *dl,
                                   AVS_LIST(anjay_download_ctx_t) *out_dl_ctx,
                                   const anjay_download_config_t *cfg,
                                   uintptr_t id);

#ifdef ANJAY_WITH_COAP_DOWNLOAD
anjay_downloader_ctx_constructor_t _anjay_downloader_coap_ctx_new;
#endif // ANJAY_WITH_COAP_DOWNLOAD

#ifdef ANJAY_WITH_HTTP_DOWNLOAD
anjay_downloader_ctx_constructor_t _anjay_downloader_http_ctx_new;
#endif // ANJAY_WITH_HTTP_DOWNLOAD

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_DOWNLOADER_PRIVATE_H */
