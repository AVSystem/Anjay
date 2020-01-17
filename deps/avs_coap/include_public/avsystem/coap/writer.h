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

#ifndef AVSYSTEM_COAP_WRITER_H
#define AVSYSTEM_COAP_WRITER_H

#include <avsystem/coap/config.h>

#include <avsystem/commons/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Handler that generates payload to be sent with a streaming request.
 *
 * [BLOCK] whenever avs_stream_write on @p out_stream fills up an entire BLOCK,
 * it blocks the execution until receiving confirmation or exhausting all
 * retransmissions.
 *
 * @param out_stream Stream to write the payload to. MUST NOT be released by
 *                   the handler.
 *
 * @param arg        Opaque user-defined data.
 *
 * @returns 0 on success, a negative value in case of error.
 *
 * TODO: what should happen when we receive a response to an async request
 * while handling a streaming request/response? Possibilities include:
 *
 * - Ignoring the response and hoping we'll get the response again after next
 *   retransmission is not feasible. On reliable transports like TCP we will
 *   not get another chance of handling such response.
 *
 * 1. Attempting to handle the response anyway. This could have fatal
 *    consequences if the async response handler changes some program state
 *    that the streaming API assumes to be immutable.
 *
 * 2. Caching the whole response until the streaming request is finished,
 *    sacrificing a bit of RAM. For that we'd probably need to throttle the
 *    number of concurrent CoAP exchanges sent to the server to avoid having
 *    to cache too many responses.
 *
 * 3. A hybrid of 1 & 2: calling the user-defined handler, but allowing the
 *    user to request caching the response for later processing if they detect
 *    processing the response now could be dangerous.
 */
typedef int avs_coap_streaming_writer_t(avs_stream_t *out_stream, void *arg);

/**
 * Callback that is called via the scheduler whenever the library needs payload
 * data to send for a CoAP exchange configured using
 * @c avs_coap_async_send_request , @c avs_coap_async_setup_response
 * or @c avs_coap_async_notify .
 *
 * @param[in]    payload_offset         Offset (in bytes) within the CoAP
 *                                      response payload that the data provided
 *                                      by the function into @p payload_buf will
 *                                      correspond to. This is an absolute
 *                                      offset within the same domain as the
 *                                      corresponding BLOCK option value, if
 *                                      applicable and sent.
 *
 * @param[inout] payload_buf            Pointer to a buffer of
 *                                      @p payload_buf_size bytes that the
 *                                      function is supposed to fill with a
 *                                      chunk of payload data.
 *
 * @param[in]    payload_buf_size       Number of bytes allocated within
 *                                      @p payload_buf .
 *
 * @param[out]   out_payload_chunk_size Pointer to a variable that SHOULD be set
 *                                      to a number of bytes actually written
 *                                      into @p payload_buf . On entry, that
 *                                      variable is guaranteed to be zero.
 *
 * @param[in]    arg                    Opaque user-defined data.
 *
 * @returns
 * - 0 on success.
 *   - If @p out_payload_chunk_size is set to less than @p payload_buf_size
 *     (including zero), this is treated as end of payload. This function will
 *     never be called again for a given exchange. The library will proceed to
 *     receiving the response, and start calling
 *     @ref avs_coap_async_response_handler_t accordingly.
 *   - If @p out_payload_chunk_size is set to exactly @p payload_buf_size, this
 *     function will be called again later with @p payload_offset increased by
 *     @p payload_buf_size, requesting more data.
 *   - The result when @p out_payload_chunk_size is set to a value greater than
 *     @p payload_buf_size is undefined. On debug builds, the program will abort
 *     due to a failed assertion.
 * - A non-zero value in case of error. The library will cancel the exchange in
 *   a transport-specific way. In case of an outgoing request,
 *   @ref avs_coap_async_response_handler_t will be called with
 *   @ref AVS_COAP_EXCHANGE_CANCEL result argument.
 */
typedef int avs_coap_payload_writer_t(size_t payload_offset,
                                      void *payload_buf,
                                      size_t payload_buf_size,
                                      size_t *out_payload_chunk_size,
                                      void *arg);

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_WRITER_H
