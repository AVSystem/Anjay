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

#ifndef AVSYSTEM_COAP_CODE_H
#define AVSYSTEM_COAP_CODE_H

#include <avsystem/coap/avs_coap_config.h>

#include <stdbool.h>
#include <stdint.h>

#include <avsystem/commons/avs_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Internal macros used for constructing/parsing CoAP codes.
 * @{
 */
#define _AVS_COAP_CODE_CLASS_MASK 0xE0
#define _AVS_COAP_CODE_CLASS_SHIFT 5
#define _AVS_COAP_CODE_DETAIL_MASK 0x1F
#define _AVS_COAP_CODE_DETAIL_SHIFT 0

#define AVS_COAP_CODE(cls, detail)                                       \
    ((((cls) << _AVS_COAP_CODE_CLASS_SHIFT) & _AVS_COAP_CODE_CLASS_MASK) \
     | (((detail) << _AVS_COAP_CODE_DETAIL_SHIFT)                        \
        & _AVS_COAP_CODE_DETAIL_MASK))
/** @} */

/**
 * @name avs_coap_code_constants
 *
 * CoAP code constants, as defined in RFC7252/RFC7959.
 *
 * For detailed description of their semantics, refer to appropriate RFCs.
 * @{
 */
// clang-format off
#define AVS_COAP_CODE_EMPTY  AVS_COAP_CODE(0, 0)

#define AVS_COAP_CODE_GET    AVS_COAP_CODE(0, 1)
#define AVS_COAP_CODE_POST   AVS_COAP_CODE(0, 2)
#define AVS_COAP_CODE_PUT    AVS_COAP_CODE(0, 3)
#define AVS_COAP_CODE_DELETE AVS_COAP_CODE(0, 4)
/** https://tools.ietf.org/html/rfc8132#section-4 */
#define AVS_COAP_CODE_FETCH  AVS_COAP_CODE(0, 5)
#define AVS_COAP_CODE_PATCH  AVS_COAP_CODE(0, 6)
#define AVS_COAP_CODE_IPATCH AVS_COAP_CODE(0, 7)

#define AVS_COAP_CODE_CREATED  AVS_COAP_CODE(2, 1)
#define AVS_COAP_CODE_DELETED  AVS_COAP_CODE(2, 2)
#define AVS_COAP_CODE_VALID    AVS_COAP_CODE(2, 3)
#define AVS_COAP_CODE_CHANGED  AVS_COAP_CODE(2, 4)
#define AVS_COAP_CODE_CONTENT  AVS_COAP_CODE(2, 5)
#define AVS_COAP_CODE_CONTINUE AVS_COAP_CODE(2, 31)

#define AVS_COAP_CODE_BAD_REQUEST                AVS_COAP_CODE(4, 0)
#define AVS_COAP_CODE_UNAUTHORIZED               AVS_COAP_CODE(4, 1)
#define AVS_COAP_CODE_BAD_OPTION                 AVS_COAP_CODE(4, 2)
#define AVS_COAP_CODE_FORBIDDEN                  AVS_COAP_CODE(4, 3)
#define AVS_COAP_CODE_NOT_FOUND                  AVS_COAP_CODE(4, 4)
#define AVS_COAP_CODE_METHOD_NOT_ALLOWED         AVS_COAP_CODE(4, 5)
#define AVS_COAP_CODE_NOT_ACCEPTABLE             AVS_COAP_CODE(4, 6)
#define AVS_COAP_CODE_REQUEST_ENTITY_INCOMPLETE  AVS_COAP_CODE(4, 8)
#define AVS_COAP_CODE_PRECONDITION_FAILED        AVS_COAP_CODE(4, 12)
#define AVS_COAP_CODE_REQUEST_ENTITY_TOO_LARGE   AVS_COAP_CODE(4, 13)
#define AVS_COAP_CODE_UNSUPPORTED_CONTENT_FORMAT AVS_COAP_CODE(4, 15)

#define AVS_COAP_CODE_INTERNAL_SERVER_ERROR  AVS_COAP_CODE(5, 0)
#define AVS_COAP_CODE_NOT_IMPLEMENTED        AVS_COAP_CODE(5, 1)
#define AVS_COAP_CODE_BAD_GATEWAY            AVS_COAP_CODE(5, 2)
#define AVS_COAP_CODE_SERVICE_UNAVAILABLE    AVS_COAP_CODE(5, 3)
#define AVS_COAP_CODE_GATEWAY_TIMEOUT        AVS_COAP_CODE(5, 4)
#define AVS_COAP_CODE_PROXYING_NOT_SUPPORTED AVS_COAP_CODE(5, 5)
// clang-format on
/** @} */

/**
 * CoAP code class accessor. See RFC7252 for details.
 */
uint8_t avs_coap_code_get_class(uint8_t code);

/**
 * CoAP code detail accessor. See RFC7252 for details.
 */
uint8_t avs_coap_code_get_detail(uint8_t code);

/**
 * @returns true if @p code belongs to a "client error" code class,
 *          false otherwise.
 */
bool avs_coap_code_is_client_error(uint8_t code);

/**
 * @returns true if @p code belongs to a "server error" code class,
 *          false otherwise.
 */
bool avs_coap_code_is_server_error(uint8_t code);

/**
 * @returns true if @p code is either a "server error" or a "client error"
 */
static inline bool avs_coap_code_is_error(uint8_t code) {
    return avs_coap_code_is_server_error(code)
           || avs_coap_code_is_client_error(code);
}

/**
 * @returns true if @p code belongs to a "success" code class, false otherwise.
 */
bool avs_coap_code_is_success(uint8_t code);

/**
 * @returns true if @p code represents a request, false otherwise.
 *          Note: Empty (0.00) is NOT considered a request. See RFC7252
 *          for details.
 */
bool avs_coap_code_is_request(uint8_t code);

/** @returns true if @p code represents a response, false otherwise. */
bool avs_coap_code_is_response(uint8_t code);

/**
 * Converts CoAP code to a human-readable form.
 *
 * @param code     CoAP code to convert.
 * @param buf      Buffer to store the code string in.
 * @param buf_size @p buf capacity, in bytes.
 *
 * @returns @p buf on success, a pointer to a implementation-defined constant
 *          string if @p code is unknown or @p buf is too small.
 */
const char *avs_coap_code_to_string(uint8_t code, char *buf, size_t buf_size);

/**
 * Convenience macro that calls @ref avs_coap_msg_code_to_string with
 * a stack-allocated buffer big enough to store any CoAP code string.
 */
#define AVS_COAP_CODE_STRING(Code) \
    avs_coap_code_to_string((Code), &(char[32]){ 0 }[0], 32)

#ifdef __cplusplus
}
#endif

#endif // AVSYSTEM_COAP_CODE_H
