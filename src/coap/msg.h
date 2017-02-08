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

#ifndef ANJAY_COAP_MSG_H
#define ANJAY_COAP_MSG_H

#include <assert.h>
#include <stdint.h>

#include <avsystem/commons/defs.h>

#include "opt.h"
#include "utils.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#define ANJAY_COAP_MSG_MIN_SIZE ((unsigned) sizeof(anjay_coap_msg_header_t))
#define ANJAY_COAP_MSG_BLOCK_MIN_SIZE (1 << 4)
#define ANJAY_COAP_MSG_BLOCK_MAX_SIZE (1 << 10)
#define ANJAY_COAP_MAX_TOKEN_LENGTH 8

// Technically, CoAP options may contain up to 2 bytes of extended option number
// and up to 2 bytes of extended length. This should never be required for BLOCK
// options. Why? 2-byte extended values are required for interpreting values
// >= 269. BLOCK uses 23/27 option numbers and allows up to 3 content bytes.
// Therefore correct BLOCK options will use at most 1 byte for extended number
// (since wrapping is not allowed) and will never use extended length field.
#define ANJAY_COAP_OPT_BLOCK_MAX_SIZE (1    /* option header   */ \
                                       + 1  /* extended number */ \
                                       + 3) /* block option value */

typedef struct {
    char bytes[ANJAY_COAP_MAX_TOKEN_LENGTH];
} anjay_coap_token_t;

#define ANJAY_COAP_TOKEN_EMPTY ((anjay_coap_token_t){{0}})

typedef struct anjay_coap_msg_identity {
    uint16_t msg_id;
    anjay_coap_token_t token;
    size_t token_size;
} anjay_coap_msg_identity_t;

#define ANJAY_COAP_MSG_IDENTITY_EMPTY ((anjay_coap_msg_identity_t){0,{{0}},0})

typedef enum anjay_coap_msg_type {
    ANJAY_COAP_MSG_CONFIRMABLE,
    ANJAY_COAP_MSG_NON_CONFIRMABLE,
    ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
    ANJAY_COAP_MSG_RESET,

    _ANJAY_COAP_MSG_FIRST = ANJAY_COAP_MSG_CONFIRMABLE,
    _ANJAY_COAP_MSG_LAST = ANJAY_COAP_MSG_RESET
} anjay_coap_msg_type_t;

#define ANJAY_COAP_CODE_CLASS_MASK 0xE0
#define ANJAY_COAP_CODE_CLASS_SHIFT 5
#define ANJAY_COAP_CODE_DETAIL_MASK 0x1F
#define ANJAY_COAP_CODE_DETAIL_SHIFT 0

#define ANJAY_COAP_CODE(cls, detail) \
    ((((cls) << ANJAY_COAP_CODE_CLASS_SHIFT) \
      & ANJAY_COAP_CODE_CLASS_MASK) \
     | (((detail) << ANJAY_COAP_CODE_DETAIL_SHIFT) \
        & ANJAY_COAP_CODE_DETAIL_MASK))

#define ANJAY_COAP_CODE_EMPTY  ANJAY_COAP_CODE(0, 0)

#define ANJAY_COAP_CODE_GET    ANJAY_COAP_CODE(0, 1)
#define ANJAY_COAP_CODE_POST   ANJAY_COAP_CODE(0, 2)
#define ANJAY_COAP_CODE_PUT    ANJAY_COAP_CODE(0, 3)
#define ANJAY_COAP_CODE_DELETE ANJAY_COAP_CODE(0, 4)

#define ANJAY_COAP_CODE_CREATED  ANJAY_COAP_CODE(2, 1)
#define ANJAY_COAP_CODE_DELETED  ANJAY_COAP_CODE(2, 2)
#define ANJAY_COAP_CODE_VALID    ANJAY_COAP_CODE(2, 3)
#define ANJAY_COAP_CODE_CHANGED  ANJAY_COAP_CODE(2, 4)
#define ANJAY_COAP_CODE_CONTENT  ANJAY_COAP_CODE(2, 5)
#define ANJAY_COAP_CODE_CONTINUE ANJAY_COAP_CODE(2, 31)

#define ANJAY_COAP_CODE_BAD_REQUEST                ANJAY_COAP_CODE(4, 0)
#define ANJAY_COAP_CODE_UNAUTHORIZED               ANJAY_COAP_CODE(4, 1)
#define ANJAY_COAP_CODE_BAD_OPTION                 ANJAY_COAP_CODE(4, 2)
#define ANJAY_COAP_CODE_FORBIDDEN                  ANJAY_COAP_CODE(4, 3)
#define ANJAY_COAP_CODE_NOT_FOUND                  ANJAY_COAP_CODE(4, 4)
#define ANJAY_COAP_CODE_METHOD_NOT_ALLOWED         ANJAY_COAP_CODE(4, 5)
#define ANJAY_COAP_CODE_NOT_ACCEPTABLE             ANJAY_COAP_CODE(4, 6)
#define ANJAY_COAP_CODE_REQUEST_ENTITY_INCOMPLETE  ANJAY_COAP_CODE(4, 8)
#define ANJAY_COAP_CODE_PRECONDITION_FAILED        ANJAY_COAP_CODE(4, 12)
#define ANJAY_COAP_CODE_REQUEST_ENTITY_TOO_LARGE   ANJAY_COAP_CODE(4, 13)
#define ANJAY_COAP_CODE_UNSUPPORTED_CONTENT_FORMAT ANJAY_COAP_CODE(4, 15)

#define ANJAY_COAP_CODE_INTERNAL_SERVER_ERROR  ANJAY_COAP_CODE(5, 0)
#define ANJAY_COAP_CODE_NOT_IMPLEMENTED        ANJAY_COAP_CODE(5, 1)
#define ANJAY_COAP_CODE_BAD_GATEWAY            ANJAY_COAP_CODE(5, 2)
#define ANJAY_COAP_CODE_SERVICE_UNAVAILABLE    ANJAY_COAP_CODE(5, 3)
#define ANJAY_COAP_CODE_GATEWAY_TIMEOUT        ANJAY_COAP_CODE(5, 4)
#define ANJAY_COAP_CODE_PROXYING_NOT_SUPPORTED ANJAY_COAP_CODE(5, 5)

const char *_anjay_coap_msg_code_to_string(uint8_t code,
                                           char *buf,
                                           size_t buf_size);

#define ANJAY_COAP_CODE_STRING(Code) \
        _anjay_coap_msg_code_to_string((Code), &(char[32]){0}[0], 32)

static inline uint8_t _anjay_coap_msg_code_get_class(const uint8_t *code) {
    return ANJAY_FIELD_GET(*code, ANJAY_COAP_CODE_CLASS_MASK,
                           ANJAY_COAP_CODE_CLASS_SHIFT);
}
static inline void _anjay_coap_msg_code_set_class(uint8_t *code,
                                                  uint8_t cls) {
    assert(cls < 8);
    ANJAY_FIELD_SET(*code, ANJAY_COAP_CODE_CLASS_MASK,
                    ANJAY_COAP_CODE_CLASS_SHIFT, cls);
}
static inline uint8_t _anjay_coap_msg_code_get_detail(const uint8_t *code) {
    return ANJAY_FIELD_GET(*code, ANJAY_COAP_CODE_DETAIL_MASK,
                           ANJAY_COAP_CODE_DETAIL_SHIFT);
}
static inline void _anjay_coap_msg_code_set_detail(uint8_t *code,
                                                   uint8_t detail) {
    assert(detail < 32);
    ANJAY_FIELD_SET(*code, ANJAY_COAP_CODE_DETAIL_MASK,
                    ANJAY_COAP_CODE_DETAIL_SHIFT, detail);
}

static inline bool _anjay_coap_msg_code_is_client_error(uint8_t code) {
    return _anjay_coap_msg_code_get_class(&code) == 4;
}

static inline bool _anjay_coap_msg_code_is_server_error(uint8_t code) {
    return _anjay_coap_msg_code_get_class(&code) == 5;
}

typedef struct anjay_coap_msg_header {
    uint8_t version_type_token_length;
    uint8_t code;
    uint8_t message_id[2];
} anjay_coap_msg_header_t;

AVS_STATIC_ASSERT(offsetof(anjay_coap_msg_header_t, version_type_token_length) == 0,
                  vttl_field_is_at_start_of_msg_header_t);
AVS_STATIC_ASSERT(offsetof(anjay_coap_msg_header_t, code) == 1,
                  no_padding_before_code_field_of_msg_header_t);
AVS_STATIC_ASSERT(offsetof(anjay_coap_msg_header_t, message_id) == 2,
                  no_padding_before_message_id_field_of_msg_header_t);
AVS_STATIC_ASSERT(sizeof(anjay_coap_msg_header_t) == 4,
                  no_padding_in_msg_header_t);

#define ANJAY_COAP_HEADER_TYPE_MASK 0x30
#define ANJAY_COAP_HEADER_TYPE_SHIFT 4

static inline anjay_coap_msg_type_t
_anjay_coap_msg_header_get_type(const anjay_coap_msg_header_t *hdr) {
    int val = ANJAY_FIELD_GET(hdr->version_type_token_length,
                              ANJAY_COAP_HEADER_TYPE_MASK,
                              ANJAY_COAP_HEADER_TYPE_SHIFT);
    assert(val >= _ANJAY_COAP_MSG_FIRST && val <= _ANJAY_COAP_MSG_LAST);
    return (anjay_coap_msg_type_t)val;
}
static inline void
_anjay_coap_msg_header_set_type(anjay_coap_msg_header_t *hdr,
                                anjay_coap_msg_type_t type) {
    ANJAY_FIELD_SET(hdr->version_type_token_length,
                    ANJAY_COAP_HEADER_TYPE_MASK,
                    ANJAY_COAP_HEADER_TYPE_SHIFT, type);
}

typedef struct anjay_coap_msg {
    uint32_t length; // whole message (header + content)
    anjay_coap_msg_header_t header;
    uint8_t content[]; // token + opts + payload
} anjay_coap_msg_t;

/* There seems to be no portable way to implement alignof()
 * for structs with FAM before C11 */
#define _ANJAY_COAP_MSG_ALIGNMENT sizeof(uint32_t)

AVS_STATIC_ASSERT(offsetof(anjay_coap_msg_t, header) == 4,
                  no_padding_before_header_field_of_msg_t);
AVS_STATIC_ASSERT(offsetof(anjay_coap_msg_t, content) == 8,
                  no_padding_before_content_field_of_msg_t);

typedef struct anjay_coap_opt_iterator {
    const anjay_coap_msg_t *const msg;
    const anjay_coap_opt_t *curr_opt;
    uint32_t prev_opt_number;
} anjay_coap_opt_iterator_t;

#define ANJAY_COAP_OPT_ITERATOR_EMPTY \
    (anjay_coap_opt_iterator_t) { \
        NULL, NULL, 0 \
    }

/**
 * @param msg Message to retrieve ID from.
 * @returns Message ID in the host byte order.
 */
static inline uint16_t _anjay_coap_msg_get_id(const anjay_coap_msg_t *msg) {
    return extract_u16((const uint8_t *) &msg->header.message_id);
}

/**
 * @param msg Message to check
 * @return true if message is a request message (RFC7252 section 5.1),
 *      false otherwise
 */
bool _anjay_coap_msg_is_request(const anjay_coap_msg_t *msg);

/**
 * @param msg       Message to retrieve token from.
 * @param out_token Buffer for the extracted token.
 * @returns Token length in bytes (0 <= length <= sizeof(anjay_coap_token_t)).
 */
size_t _anjay_coap_msg_get_token(const anjay_coap_msg_t *msg,
                                 anjay_coap_token_t *out_token);

/**
 * @param msg Message to iterate over.
 * @returns An CoAP Option iterator object.
 */
anjay_coap_opt_iterator_t _anjay_coap_opt_begin(const anjay_coap_msg_t *msg);

/**
 * Advances the @p optit iterator to the next CoAP Option.
 *
 * @param optit CoAP Option iterator to operate on.
 * @returns @p optit.
 */
anjay_coap_opt_iterator_t *
_anjay_coap_opt_next(anjay_coap_opt_iterator_t *optit);

/**
 * Checks if the @p optit points to the area after CoAP options list.
 *
 * @param optit Iterator to check.
 * @returns true if there are no more Options to iterate over (i.e. the iterator
 *          is invalidated), false if it points to a valid Option.
 */
bool _anjay_coap_opt_end(const anjay_coap_opt_iterator_t *optit);

/**
 * @param optit Iterator to operate on.
 * @returns Number of the option currently pointed to by @p optit
 */
uint32_t _anjay_coap_opt_number(const anjay_coap_opt_iterator_t *optit);

/**
 * Returns the number of CoAP Options in the message. Note: this iterates over
 * all the options (O(n) complexty).
 *
 * Note: this function is NOT SAFE to use on invalid messages.
 *
 * @param msg Message to operate on.
 * @returns Number of CoAP options in the @p msg.
 */
size_t _anjay_coap_msg_count_opts(const anjay_coap_msg_t *msg);

/**
 * Note: this function is NOT SAFE to use on invalid messages.
 *
 * @param      msg        Message to operate on.
 * @param      opt_number Number of the option to find.
 * @param[out] out_opt    Returned option. Set to the first option with given
 *                        number present in @p msg or to NULL if such option was
 *                        not found.
 *
 * @returns 0 if the option was found and is unique,
 *          -1 if the option does not exist or multiple options with given
 *          @p opt_number were found. Inspect @p out_opt to determine which
 *          one was the case.
 */
int _anjay_coap_msg_find_unique_opt(const anjay_coap_msg_t *msg,
                                    uint16_t opt_number,
                                    const anjay_coap_opt_t **out_opt);

/**
 * Note: this function is NOT SAFE to use on invalid messages.
 *
 * @param msg Message to operate on.
 * @returns Pointer to the start of a message payload, or end-of-message if it
 *          does not contain payload.
 */
const void *_anjay_coap_msg_payload(const anjay_coap_msg_t *msg);

/**
 * Note: this function is NOT SAFE to use on invalid messages.
 *
 * @param msg Message to operate on.
 * @returns Message payload size in bytes.
 */
size_t _anjay_coap_msg_payload_length(const anjay_coap_msg_t *msg);

/**
 * @param msg Message to operate on.
 * @returns true if the message has a valid format, false otherwise.
 */
bool _anjay_coap_msg_is_valid(const anjay_coap_msg_t *msg);

/**
 * Prints the @p msg content to standard output.
 *
 * @param msg Message to print.
 */
void _anjay_coap_msg_debug_print(const anjay_coap_msg_t *msg);

/**
 * Prints a short summary of the @p msg to @p buf and returns a pointer
 * to @p buf.
 *
 * @param msg      Message to summarize.
 * @param buf      Buffer to put summary in.
 * @param buf_size @p buf capacity, in bytes.
 *
 * @returns @p buf.
 */
const char *_anjay_coap_msg_summary(const anjay_coap_msg_t *msg,
                                    char *buf,
                                    size_t buf_size);

#define ANJAY_COAP_MSG_SUMMARY(Msg) \
        _anjay_coap_msg_summary((Msg), &(char[256]){0}[0], 256)

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_MSG_H

