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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_UTILS_H
#define ANJAY_INCLUDE_ANJAY_MODULES_UTILS_H

#include <config.h>

#include <stdio.h>

#ifdef WITH_AVS_LOG
#include <avsystem/commons/log.h>
#define _anjay_log(...) avs_log(__VA_ARGS__)
#else
#define _anjay_log(...) ((void) 0)
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

#define ANJAY_ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

typedef struct {
    void *data;
    /** Amount of bytes currently stored in the buffer. */
    size_t size;
    /** Amount of bytes that might be stored in the buffer. */
    size_t capacity;
} anjay_raw_buffer_t;

#define ANJAY_RAW_BUFFER_ON_STACK(Capacity) \
    (anjay_raw_buffer_t) { \
        .data = &(uint8_t[Capacity]){0}[0], \
        .size = 0, \
        .capacity = Capacity \
    }

#define ANJAY_RAW_BUFFER_EMPTY \
    (anjay_raw_buffer_t) { \
        .data = NULL, \
        .size = 0, \
        .capacity = 0 \
    }

/**
 * Calls free() on buffer->data and resets its state by setting buffer->data
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
 * Creates heap raw buffer by copying data pointed by @p data.
 * @returns 0 on success, negative value if no memory is available
 */
int _anjay_raw_buffer_from_data(anjay_raw_buffer_t *dst,
                                const void *src,
                                size_t size);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_UTILS_H */

