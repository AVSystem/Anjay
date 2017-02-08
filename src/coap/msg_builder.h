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

#ifndef ANJAY_COAP_MSGBUILDER_H
#define ANJAY_COAP_MSGBUILDER_H

#include <stdlib.h>
#include <assert.h>

#include "msg_info.h"
#include "../utils.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct anjay_coap_msg_buffer {
    anjay_coap_msg_t *msg;
    size_t capacity;
} anjay_coap_msg_buffer_t;

typedef struct anjay_coap_msg_builder {
    bool has_payload_marker;
    anjay_coap_msg_buffer_t msg_buffer;
} anjay_coap_msg_builder_t;

#define ANJAY_COAP_MSG_BUILDER_UNINITIALIZED \
    ((anjay_coap_msg_builder_t){ \
        .has_payload_marker = false, \
        .msg_buffer = { .msg = NULL, .capacity = 0 } \
    })

static inline bool
_anjay_coap_msg_builder_is_initialized(anjay_coap_msg_builder_t *builder) {
    return builder->msg_buffer.msg != NULL;
}

static inline bool
_anjay_coap_msg_builder_has_payload(anjay_coap_msg_builder_t *builder) {
    return builder->has_payload_marker;
}

/* the struct itself is not defined, as the pointer is never defererenced */
typedef struct anjay_coap_aligned_msg_buffer anjay_coap_aligned_msg_buffer_t;

static inline anjay_coap_aligned_msg_buffer_t *
_anjay_coap_ensure_aligned_buffer(void *buffer) {
    assert((uintptr_t)buffer % _ANJAY_COAP_MSG_ALIGNMENT == 0
           && "the buffer MUST have the same alignment as anjay_coap_msg_t");

    return (anjay_coap_aligned_msg_buffer_t *)buffer;
}

/**
 * Creates an @ref anjay_coap_msg_builder_t backed by @p buffer . @p buffer MUST
 * live at least as long as @p builder.
 *
 * @param[out] builder     Builder to initialize.
 * @param[in]  buffer      Buffer to use as storage for constructed CoAP
 *                         message. WARNING: this buffer MUST have the same
 *                         alignment as @ref anjay_coap_msg_t .
 * @param[in]  buffer_size Number of bytes available in @p buffer.
 * @param[in]  header      Set of headers to initialize @p builder with.
 *
 * @returns 0 on success, a negative value in case of error (including incorrect
 *          alignment of @p buffer).
 */
int _anjay_coap_msg_builder_init(anjay_coap_msg_builder_t *builder,
                                 anjay_coap_aligned_msg_buffer_t *buffer,
                                 size_t buffer_size_bytes,
                                 const anjay_coap_msg_info_t *info);

#define ANJAY_COAP_BLOCK_MAX_SEQ_NUMBER 0xFFFFF

/**
 * Initializes a @p builder with message headers stored in @p header. Resets any
 * payload possibly written to @p builder.
 *
 * @param builder Builder object to reset.
 * @param header  Set of headers to initialize @p builder with.
 *
 * @return 0 on success, a negative value in case of error.
 */
int _anjay_coap_msg_builder_reset(anjay_coap_msg_builder_t *builder,
                                  const anjay_coap_msg_info_t *info);

/**
 * Returns amount of bytes that can be written as a payload using this builder
 * instance.
 *
 * @param builder      Builder object to operate on.
 *
 * @return Number of bytes available for the payload.
 */
size_t _anjay_coap_msg_builder_payload_remaining(
        const anjay_coap_msg_builder_t *builder);

/**
 * Appends at most @p payload_size bytes of @p payload to the message
 * being built.
 *
 * @param builder      Builder object to operate on.
 * @param payload      Payload bytes to append.
 * @param payload_size Number of bytes in the @p payload buffer.
 *
 * @return Number of bytes written. NOTE: this may be less than @p payload_size.
 */
size_t _anjay_coap_msg_builder_payload(anjay_coap_msg_builder_t *builder,
                                       const void *payload,
                                       size_t payload_size);

/**
 * Finalizes creation of the message. At least message header MUST be set in
 * order for this function to succeed.
 *
 * This function does not consume the builder. Repeated calls create identical
 * messages.
 *
 * @param builder Builder object to retrieve a message from.
 *
 * @return Pointer to a @ref anjay_coap_msg_t object stored in the @p builder
 *         buffer. The message is guaranteed to be syntactically valid.
 *         This function always returns a non-NULL pointer to a serialized
 *         message build in @p buffer .
 */
const anjay_coap_msg_t *
_anjay_coap_msg_builder_get_msg(const anjay_coap_msg_builder_t *builder);

/**
 * Helper function for building messages with no payload.
 *
 * @param buffer      Buffer to store the message in.
 * @param buffer_size Number of bytes available in @p buffer.
 * @param header      Message headers.
 *
 * @return Constructed message object on success, NULL in case of error.
 */
static inline const anjay_coap_msg_t *
_anjay_coap_msg_build_without_payload(anjay_coap_aligned_msg_buffer_t *buffer,
                                      size_t buffer_size,
                                      const anjay_coap_msg_info_t *info) {
    anjay_coap_msg_builder_t builder;
    if (_anjay_coap_msg_builder_init(&builder, buffer, buffer_size, info)) {
        assert(0 && "could not initialize msg builder");
        return NULL;
    }

    return _anjay_coap_msg_builder_get_msg(&builder);
}

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_MSGBUILDER_H
