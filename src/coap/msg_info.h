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

#ifndef ANJAY_COAP_MSGINFO_H
#define ANJAY_COAP_MSGINFO_H

#include <stdlib.h>

#include <avsystem/commons/list.h>

#include "msg.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct anjay_coap_msg_info_opt anjay_coap_msg_info_opt_t;

typedef struct anjay_coap_msg_info {
    anjay_coap_msg_type_t type;
    uint8_t code;
    anjay_coap_msg_identity_t identity;

    /* Fields below are NOT meant to be modified directly. Use provided
     * accessor functions instead. */
    AVS_LIST(anjay_coap_msg_info_opt_t) options_;
} anjay_coap_msg_info_t;

/**
 * Initializes a @ref anjay_coap_header_msg_info_t .
 */
#define _anjay_coap_msg_info_init() \
    ((anjay_coap_msg_info_t){ \
        .code = ANJAY_COAP_CODE_EMPTY, \
        .identity = { \
            .msg_id = 0, \
            .token = {{0}}, \
            .token_size = 0 \
        }, \
        .options_ = NULL \
    })

/**
 * Frees any memory allocated for temporary storage required by the info object.
 * Resets all header fields to defaults.
 */
void _anjay_coap_msg_info_reset(anjay_coap_msg_info_t *info);

/**
 * Calculates number of header bytes in the CoAP packet constructed from the
 * @p info struct.
 *
 * @returns total number of bytes of a message that will be actually transmitted
 *          over the wire.
 *
 * NOTE: Unlike @ref _anjay_coap_msg_info_get_storage_size , this DOES NOT
 * include the size of @ref anjay_coap_msg_info_t#length field. Because of that
 * this function is NOT suitable for calculating size of the buffer for a
 * serialized message.
 */
size_t _anjay_coap_msg_info_get_headers_size(const anjay_coap_msg_info_t *info);

/**
 * Calculates number of bytes required to serialize the message stored in a
 * @ref anjay_coap_msg_info_t object.
 *
 * @returns total number of bytes required for serialized message, assuming
 *          no payload and a token of maximum possible size.
 *
 * NOTE: This includes the @ref anjay_coap_msg_info_t length field size.
 */
size_t _anjay_coap_msg_info_get_storage_size(const anjay_coap_msg_info_t *info);

/**
 * @returns total number of bytes of a serialized message that will be sent over
 *          the wire, assuming no payload and a token of maximum possible size.
 */
static inline size_t
_anjay_coap_msg_info_get_max_mtu_overhead(const anjay_coap_msg_info_t *info) {
    return _anjay_coap_msg_info_get_storage_size(info)
           - offsetof(anjay_coap_msg_t, header);
}

/**
 * @returns total number of bytes required for serialized message, assuming
 *          @p block_size bytes of payload and a token of maximum possible size.
 */
size_t
_anjay_coap_msg_info_get_packet_storage_size(const anjay_coap_msg_info_t *info,
                                             size_t payload_size);

/**
 * Removes all options with given @p option_number added to @p info.
 */
void _anjay_coap_msg_info_opt_remove_by_number(anjay_coap_msg_info_t *info,
                                               uint16_t option_number);

/** Auxiliary constants for common Content-Format Option values */

#define ANJAY_COAP_FORMAT_APPLICATION_LINK 40

/**
 * TODO: following numbers are not yet registered by IANA
 * See https://github.com/OpenMobileAlliance/OMA-LwM2M-Public-Review/issues/26
 */
#define ANJAY_COAP_FORMAT_PLAINTEXT 0
#define ANJAY_COAP_FORMAT_OPAQUE 42
#define ANJAY_COAP_FORMAT_TLV 11542
#define ANJAY_COAP_FORMAT_JSON 11543

#ifdef WITH_LEGACY_CONTENT_FORMAT_SUPPORT
#define ANJAY_COAP_FORMAT_LEGACY_PLAINTEXT 1541
#define ANJAY_COAP_FORMAT_LEGACY_TLV 1542
#define ANJAY_COAP_FORMAT_LEGACY_JSON 1543
#define ANJAY_COAP_FORMAT_LEGACY_OPAQUE 1544
#endif // WITH_LEGACY_CONTENT_FORMAT_SUPPORT

/**
 * A magic value used to indicate the absence of the Content-Format option.
 * Mainly used during CoAP message parsing, passing it to the info object does
 * nothing.
 * */
#define ANJAY_COAP_FORMAT_NONE 65535

/**
 * Adds a Content-Format Option (@ref ANJAY_COAP_OPT_CONTENT_FORMAT = 12) to the
 * message being built.
 *
 * @param info    Info object to operate on.
 * @param format  Numeric value of the Content-Format option. May be one of the
 *                ANJAY_COAP_FORMAT_* contants. Calling this function with
 *                @ref ANJAY_COAP_FORMAT_NONE removes the Content-Format option.
 *
 * @return 0 on success, -1 in case of error.
 */
int _anjay_coap_msg_info_opt_content_format(anjay_coap_msg_info_t *info,
                                            uint16_t format);

typedef enum {
    COAP_BLOCK1,
    COAP_BLOCK2
} coap_block_type_t;

static inline uint16_t
_anjay_coap_opt_num_from_block_type(coap_block_type_t type) {
    return type == COAP_BLOCK1 ? ANJAY_COAP_OPT_BLOCK1 : ANJAY_COAP_OPT_BLOCK2;
}

typedef struct coap_block_info {
    coap_block_type_t type;
    bool valid;
    uint32_t seq_num;
    bool has_more;
    uint16_t size;
} coap_block_info_t;

/**
 * Adds the Block1 or Block2 Option to the message being built.
 *
 * @param info  Info object to operate on.
 * @param block BLOCK option content to set.
 *
 * @return 0 on success, -1 in case of error.
 */
int _anjay_coap_msg_info_opt_block(anjay_coap_msg_info_t *info,
                                   const coap_block_info_t *block);

/**
 * Adds an arbitrary CoAP option with custom value.
 *
 * Repeated calls to this function APPEND additional instances of a CoAP option.
 *
 * @param info          Info object to operate on.
 * @param opt_number    CoAP Option Number to set.
 * @param opt_data      Option value.
 * @param opt_data_size Number of bytes in the @p opt_data buffer.
 *
 * @return 0 on success, -1 in case of error:
 *         - the message code is set to @ref ANJAY_COAP_CODE_EMPTY, which must
 *           not contain any options.
 */
int _anjay_coap_msg_info_opt_opaque(anjay_coap_msg_info_t *info,
                                    uint16_t opt_number,
                                    const void *opt_data,
                                    uint16_t opt_data_size);

/**
 * Equivalent to:
 *
 * @code
 * _anjay_coap_msg_info_opt_opaque(info, opt_number,
 *                                 opt_data, strlen(opt_data))
 * @endcode
 */
int _anjay_coap_msg_info_opt_string(anjay_coap_msg_info_t *info,
                                    uint16_t opt_number,
                                    const char *opt_data);

/**
 * Adds an arbitrary CoAP option with no value.
 * See @ref _anjay_coap_msg_info_opt_opaque for more info.
 */
int _anjay_coap_msg_info_opt_empty(anjay_coap_msg_info_t *info,
                                   uint16_t opt_number);

/**
 * Functions below add an arbitrary CoAP option with an integer value. The value
 * is encoded in the most compact way available, so e.g. for @p value equal to 0
 * the option has no payload when added using any of them.
 *
 * See @ref _anjay_coap_msg_info_opt_opaque for more info.
 */
int _anjay_coap_msg_info_opt_u8(anjay_coap_msg_info_t *info,
                                uint16_t opt_number,
                                uint8_t value);
int _anjay_coap_msg_info_opt_u16(anjay_coap_msg_info_t *info,
                                 uint16_t opt_number,
                                 uint16_t value);
int _anjay_coap_msg_info_opt_u32(anjay_coap_msg_info_t *info,
                                 uint16_t opt_number,
                                 uint32_t value);
int _anjay_coap_msg_info_opt_u64(anjay_coap_msg_info_t *info,
                                 uint16_t opt_number,
                                 uint64_t value);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_MSGINFO_H
