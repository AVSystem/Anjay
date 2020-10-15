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

#ifndef ANJAY_INCLUDE_ANJAY_DOWNLOAD_H
#define ANJAY_INCLUDE_ANJAY_DOWNLOAD_H

#include <stddef.h>
#include <stdint.h>

#include <avsystem/commons/avs_net.h>

#include <anjay/anjay_config.h>
#include <anjay/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CoAP Entity Tag. */
typedef struct anjay_etag {
    uint8_t size;
    uint8_t value[1]; // actually a flexible array member
} anjay_etag_t;

/**
 * Allocates ETag with a given size.
 * The new ETag can be freed using @c avs_free.
 *
 * @param etag_size The number of bytes to be available in the returned
 *                  anjay_etag_t::value array.
 *
 * @return Pointer to created ETag, NULL on failure
 */
anjay_etag_t *anjay_etag_new(uint8_t etag_size);

/**
 * Given one ETag, creates a new one, with the same size and value.
 * The new ETag can be freed using @c avs_free.
 *
 * @param old_etag Pointer to old ETag copy
 *
 * @return Pointer to created ETag copy, NULL on failure
 */
anjay_etag_t *anjay_etag_clone(const anjay_etag_t *old_etag);

/**
 * Called each time a chunk of data is received from remote host.
 * It is guaranteed to be called with consecutive chunks of data, starting
 * from @ref anjay_download_config_t#start_offset.
 *
 * @param anjay     Anjay object managing the download process.
 * @param data      Received data.
 * @param data_size Number of bytes available in @p data .
 * @param etag      ETag option sent by the server. Should be saved if the
 *                  client may need to resume the transfer after it gets
 *                  interrupted.
 * @param user_data Value of @ref anjay_download_config_t#user_data passed
 *                  to @ref anjay_download .
 *
 * @return Should return:
 *         @li <c>AVS_OK</c> on success,
 *         @li an error value if an error occurred, in which case the download
 *             will be terminated with @ref ANJAY_DOWNLOAD_ERR_FAILED result.
 */
typedef avs_error_t
anjay_download_next_block_handler_t(anjay_t *anjay,
                                    const uint8_t *data,
                                    size_t data_size,
                                    const anjay_etag_t *etag,
                                    void *user_data);

typedef enum anjay_download_result {
    /** Download finished successfully. */
    ANJAY_DOWNLOAD_FINISHED,
    /** Download failed due to a local failure or a network error. */
    ANJAY_DOWNLOAD_ERR_FAILED,
    /** The remote server responded in a way that is permitted by the protocol,
     * but does not indicate a success (e.g. a 4xx or 5xx HTTP status). */
    ANJAY_DOWNLOAD_ERR_INVALID_RESPONSE,
    /** Downloaded resource changed while transfer was in progress. */
    ANJAY_DOWNLOAD_ERR_EXPIRED,
    /** Download was aborted by calling @ref anjay_download_abort . */
    ANJAY_DOWNLOAD_ERR_ABORTED
} anjay_download_result_t;

typedef struct {
    anjay_download_result_t result;

    union {
        /**
         * Error code. Only valid if result is ANJAY_DOWNLOAD_ERR_FAILED.
         *
         * Possible values include (but are not limited to):
         *
         * - <c>avs_errno(AVS_EADDRNOTAVAIL)</c> - DNS resolution failed
         * - <c>avs_errno(AVS_ECONNABORTED)</c> - remote resource is no longer
         *   valid
         * - <c>avs_errno(AVS_ECONNREFUSED)</c> - server responded with a reset
         *   message on the application layer (e.g. CoAP Reset)
         * - <c>avs_errno(AVS_ECONNRESET)</c> - connection lost or reset
         * - <c>avs_errno(AVS_EINVAL)</c> - could not parse response from the
         *   server
         * - <c>avs_errno(AVS_EIO)</c> - internal error in the transfer code
         * - <c>avs_errno(AVS_EMSGSIZE)</c> - could not send or receive datagram
         *   because it was too large
         * - <c>avs_errno(AVS_ENOMEM)</c> - out of memory
         * - <c>avs_errno(AVS_ETIMEDOUT)</c> - could not receive data from
         *   server in time
         */
        avs_error_t error;

        /**
         * Protocol-specific status code. Only valid if result is
         * ANJAY_DOWNLOAD_ERR_INVALID_RESPONSE.
         *
         * Currently it may be a HTTP status code (e.g. 404 or 501), or a CoAP
         * code (e.g. 132 or 161 - these examples are canonically interpreted as
         * 4.04 and 5.01, respectively). If any user log is to depend on status
         * codes, it is expected that it will be interpreted in line with the
         * URL originally passed to @ref anjay_download for the same download.
         */
        int status_code;
    } details;
} anjay_download_status_t;

/**
 * Called whenever the download finishes, successfully or not.
 *
 * @param anjay     Anjay object managing the download process.
 * @param status    Status of the download, with additional error information if
 *                  applicable.
 * @param user_data Value of @ref anjay_download_config_t#user_data passed
 *                  to @ref anjay_download .
 */
typedef void anjay_download_finished_handler_t(anjay_t *anjay,
                                               anjay_download_status_t status,
                                               void *user_data);

typedef struct anjay_download_config {
    /** Required. %coap://, %coaps://, %http:// or %https:// URL */
    const char *url;

    /**
     * If the download gets interrupted for some reason, and the client
     * is aware of how much data it managed to successfully download,
     * it can resume the transfer from a specific offset.
     */
    size_t start_offset;

    /**
     * If start_offset is not 0, etag should be set to a value returned
     * by the server during the transfer before it got interrupted.
     */
    const anjay_etag_t *etag;

    /** Required. Called after receiving a chunk of data from remote server. */
    anjay_download_next_block_handler_t *on_next_block;

    /** Required. Called after the download is finished or aborted. */
    anjay_download_finished_handler_t *on_download_finished;

    /** Opaque pointer passed to download handlers. */
    void *user_data;

    /**
     * DTLS security configuration. Required if coaps:// is used,
     * ignored for coap:// transfers.
     *
     * Contents of any data aggregated as pointers within is copied as needed,
     * so it is safe to free all related resources array after the call to
     * @ref anjay_download.
     */
    anjay_security_config_t security_config;

    /**
     * Pointer to CoAP transmission parameters object. If NULL, downloader will
     * inherit parameters from Anjay.
     */
    avs_coap_udp_tx_params_t *coap_tx_params;

} anjay_download_config_t;

typedef void *anjay_download_handle_t;

/**
 * Requests asynchronous download of an external resource.
 *
 * Download will create a new socket that will be later included in the list
 * returned by @ref anjay_get_sockets . Calling @ref anjay_serve on such socket
 * may cause calling @ref anjay_download_config_t#on_next_block if received
 * packet is the next expected chunk of downloaded data and
 * @ref anjay_download_finished_handler_t if the transfer completes or fails.
 * Request packet retransmissions are managed by Anjay scheduler, and sent by
 * @ref anjay_sched_run whenever required.
 *
 * @param anjay      Anjay object that will manage the download process.
 * @param config     Download configuration.
 * @param out_handle Pointer to a variable that will be set to a download
 *                   handle, that may be used for aborting the download.
 *                   MUST NOT be NULL.
 *
 * @returns @li <c>AVS_OK</c> on success, in which case <c>*out_handle</c> is
 *              set to a handle to the created download,
 *          @li Code of the error that happened, in which case
 *              <c>*out_handle</c> is not modified, and
 *              @ref anjay_download_config_t#on_download_finished handler is NOT
 *              called.
 */
avs_error_t anjay_download(anjay_t *anjay,
                           const anjay_download_config_t *config,
                           anjay_download_handle_t *out_handle);

/**
 * Changes the offset of the remote resource that the user wants to receive the
 * next response data block from.
 *
 * This function is only intended to be called from within an implementation of
 * @ref anjay_download_next_block_handler_t.
 *
 * The offset can only be moved forward relative to the last known starting
 * offset. Attempting to set it to an offset of byte that was already received
 * in a previously finished call to @ref anjay_download_next_block_handler_t, or
 * is smaller than an offset already passed to this function, will result in an
 * error.
 *
 * When called from within @ref anjay_download_next_block_handler_t,
 * @p next_block_offset may be set to a position that lies after or within the
 * <c>data</c> buffer passed to it (but further than the current offset). If a
 * position within the buffer is passed, the block handler will be called again
 * with a portion of the same buffer, starting at the desired offset.
 *
 * If this function is never called during a call to
 * @ref anjay_download_next_block_handler_t, the file pointer is implicitly
 * moved by the whole size of the buffer passed to it.
 *
 * It is guaranteed that if there will be a next call to
 * @ref anjay_download_next_block_handler_t for the given download, it will be
 * passed data from the specified offset.
 *
 * NOTE: Actual efficient skipping of already downloaded data is currently only
 * supported for CoAP. Using this function with HTTP downloads will only
 * suppress passing the skipped data; full file will still be transmitted over
 * the network.
 *
 * @param anjay             Anjay object managing the download process.
 * @param dl_handle         Download handle previously returned by
 *                          @ref anjay_download.
 * @param next_block_offset Block offset to set.
 *
 * @returns
 *  - @ref AVS_OK for success
 *  - <c>avs_errno(AVS_ENOENT)</c> if @p dl_handle does not refer to an existing
 *    download process
 *  - <c>avs_errno(AVS_EINVAL)</c> if @p next_block_offset is smaller than the
 *    currently recognized value
 *  - <c>avs_errno(AVS_ENOTSUP</c> if Anjay has been compiled without support
 *    for downloads
 */
avs_error_t
anjay_download_set_next_block_offset(anjay_t *anjay,
                                     anjay_download_handle_t dl_handle,
                                     size_t next_block_offset);

/**
 * Aborts a download identified by @p dl_handle. Does nothing if @p dl_handle
 * does not represent a valid download handle.
 *
 * @param anjay     Anjay object managing the download process.
 * @param dl_handle Download handle previously returned by
 *                  @ref anjay_download.
 */
void anjay_download_abort(anjay_t *anjay, anjay_download_handle_t dl_handle);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*ANJAY_INCLUDE_ANJAY_DOWNLOAD_H*/
