/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_RAW_BUFFER_H
#define ANJAY_INCLUDE_ANJAY_MODULES_RAW_BUFFER_H

#include <anjay_init.h>

#include <stdio.h>

#include <avsystem/commons/avs_defs.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    void *data;
    /** Amount of bytes currently stored in the buffer. */
    size_t size;
    /** Amount of bytes that might be stored in the buffer. */
    size_t capacity;
} anjay_raw_buffer_t;

#define ANJAY_RAW_BUFFER_ON_STACK(Capacity)   \
    (anjay_raw_buffer_t) {                    \
        .data = &(uint8_t[Capacity]){ 0 }[0], \
        .size = 0,                            \
        .capacity = Capacity                  \
    }

#define ANJAY_RAW_BUFFER_EMPTY \
    (anjay_raw_buffer_t) {     \
        .data = NULL,          \
        .size = 0,             \
        .capacity = 0          \
    }

/**
 * Calls avs_free() on buffer->data and resets its state by setting buffer->data
 * to NULL and buffer->size to 0 and buffer->capacity to 0.
 *
 * WARNING: do not call this function if @ref anjay_raw_buffer_t was created
 * on the stack via @ref ANJAY_RAW_BUFFER_ON_STACK macro.
 */
void _anjay_raw_buffer_clear(anjay_raw_buffer_t *buffer);

/**
 * Copies data from src->data to dst->data and updates size and capacity
 * accordingly.
 *
 * @returns 0 on success, negative value if no memory is available
 */
int _anjay_raw_buffer_clone(anjay_raw_buffer_t *dst,
                            const anjay_raw_buffer_t *src);

/**
 * Creates heap raw buffer.
 * @returns 0 on success, negative value if no memory is available
 */
int _anjay_raw_buffer_alloc(anjay_raw_buffer_t *dst, size_t capacity);

/**
 * Creates heap raw buffer by copying data pointed by @p data.
 * @returns 0 on success, negative value if no memory is available
 */
int _anjay_raw_buffer_from_data(anjay_raw_buffer_t *dst,
                                const void *src,
                                size_t size);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_RAW_BUFFER_H */
