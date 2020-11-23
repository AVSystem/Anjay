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

#ifndef ANJAY_DOWNLOADER_H
#define ANJAY_DOWNLOADER_H

#include <anjay/download.h>
#include <avsystem/commons/avs_net.h>
#include <avsystem/commons/avs_sched.h>
#include <avsystem/commons/avs_stream.h>

#include "anjay_utils_private.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct anjay_download_ctx anjay_download_ctx_t;

typedef struct {
    uintptr_t next_id;
    AVS_LIST(anjay_download_ctx_t) downloads;
} anjay_downloader_t;

/**
 * Initializes a downloader object.
 *
 * @param dl                A downloader instance to initialize.
 * @param anjay             Anjay instance which owns the downloader.
 *                          Must outlive the downloader object.
 *                          @ref anjay_t#coap_id_source MUST be initialized.
 *                          @ref anjay_t#sched and @ref anjay_t#coap_socket
 *                          fields of @p anjay MUST be initialized, and MUST
 *                          not change during the downloader object lifetime.
 *
 * @returns 0 on success, negative value in case of an error.
 */
int _anjay_downloader_init(anjay_downloader_t *dl, anjay_t *anjay);

/**
 * Frees any resources associated with the downloader object. Aborts all
 * unfinished downloads, calling their @ref anjay_download_finished_handler_t
 * handlers beforehand. All scheduled retransmission jobs are canceled.
 *
 * @param dl    Pointer to the downloader object to cleanup.
 */
void _anjay_downloader_cleanup(anjay_downloader_t *dl);

/**
 * @returns currently supported values are:
 *
 * - <c>AVS_EINVAL</c> - invalid argument, i.e. unparsable URL or unset handlers
 * - <c>AVS_ENOMEM</c> - out of memory
 * - <c>AVS_EPROTO</c> - unknown error on the socket layer, including (D)TLS
 *   encryption errors
 * - <c>AVS_EPROTONOSUPPORT</c> - unsupported protocol (URL schema)
 * - <c>AVS_ETIMEDOUT</c> - attempt to connect to the remote host timed out
 * - any <c>avs_errno_t</c> value that might be set by the underlying socket
 */
avs_error_t _anjay_downloader_download(anjay_downloader_t *dl,
                                       anjay_download_handle_t *out_handle,
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
                                    avs_net_socket_t *socket);

avs_error_t
_anjay_downloader_set_next_block_offset(anjay_downloader_t *dl,
                                        anjay_download_handle_t handle,
                                        size_t next_block_offset);

void _anjay_downloader_abort(anjay_downloader_t *dl,
                             anjay_download_handle_t handle);

int _anjay_downloader_sched_reconnect(anjay_downloader_t *dl,
                                      anjay_transport_set_t transport_set);

int _anjay_downloader_sync_online_transports(anjay_downloader_t *dl);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_DOWNLOADER_H */
