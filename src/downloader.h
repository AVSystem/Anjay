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

#ifndef ANJAY_DOWNLOADER_H
#define ANJAY_DOWNLOADER_H

#include <anjay/download.h>
#include <avsystem/commons/net.h>
#include <avsystem/commons/stream.h>

#include <anjay_modules/downloader.h>

#include "coap/id_source/id_source.h"
#include "utils_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct anjay_download_ctx anjay_download_ctx_t;

typedef struct {
    coap_id_source_t *id_source;
    anjay_rand_seed_t rand_seed;

    uintptr_t next_id;
    AVS_LIST(anjay_download_ctx_t) downloads;

    anjay_sched_handle_t reconnect_job_handle;
} anjay_downloader_t;

/**
 * Initializes a downloader object.
 *
 * @param dl                A downloader instance to initialize.
 * @param anjay             Anjay instance which owns the downloader.
 *                          Must outlive the downloader object.
 *                          @ref anjay_t#sched and @ref anjay_t#coap_socket
 *                          fields of @p anjay MUST be initialized, and MUST
 *                          not change during the downloader object lifetime.
 * @param id_source_move    CoAP identity generator to use for sent requests.
 *                          Must not be NULL. If <c>*id_source_move</c> is not
 *                          NULL after a call to this function, it needs to be
 *                          released by the caller. Otherwise, the downloader
 *                          object takes ownership of the generator object and
 *                          releases it in @ref _anjay_downloader_delete .
 *
 * @returns 0 on success, negative value in case of an error.
 */
int _anjay_downloader_init(anjay_downloader_t *dl,
                           anjay_t *anjay,
                           coap_id_source_t **id_source_move);

/**
 * Frees any resources associated with the downloader object. Aborts all
 * unfinished downloads, calling their @ref anjay_download_finished_handler_t
 * handlers beforehand. All scheduled retransmission jobs are canceled.
 *
 * @param dl    Pointer to the downloader object to cleanup.
 */
void _anjay_downloader_cleanup(anjay_downloader_t *dl);

/**
 * @returns negated value of an errno constant; currently supported values are:
 *
 * - <c>-EINVAL</c> - invalid argument, i.e. unparsable URL or unset handlers
 * - <c>-ENOMEM</c> - out of memory
 * - <c>-EPROTO</c> - unknown error on the socket layer, including (D)TLS
 *   encryption errors
 * - <c>-EPROTONOSUPPORT</c> - unsupported protocol (URL schema)
 * - <c>-ETIMEDOUT</c> - attempt to connect to the remote host timed out
 * - any negated <c>errno</c> value that might be set by system calls
 *   <c>socket()</c>, <c>setsockopt()</c> <c>bind()</c>, <c>connect()</c> or
 *   <c>send()</c>
 */
int _anjay_downloader_download(anjay_downloader_t *dl,
                               anjay_download_handle_t *out,
                               const anjay_download_config_t *config);

/**
 * Retrieves all sockets used for downloads managed by @p dl and prepends them
 * to @p out_socks.
 */
int _anjay_downloader_get_sockets(anjay_downloader_t *dl,
                                  AVS_LIST(anjay_socket_entry_t) *out_socks);

/**
 * @returns @li 0 if @p socket was a downloaded socket and the incoming packet
 *              does not require further processing,
 *          @li a negative value if @p socket was not a download socket.
 *
 */
int _anjay_downloader_handle_packet(anjay_downloader_t *dl,
                                    avs_net_abstract_socket_t *socket);

void _anjay_downloader_abort(anjay_downloader_t *dl,
                             anjay_download_handle_t handle);

int _anjay_downloader_sched_reconnect_all(anjay_downloader_t *dl);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_DOWNLOADER_H */
