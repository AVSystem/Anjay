/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <anjay_modules/anjay_raw_buffer.h>

#include <avsystem/commons/avs_memory.h>

VISIBILITY_SOURCE_BEGIN

void _anjay_raw_buffer_clear(anjay_raw_buffer_t *buffer) {
    avs_free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

int _anjay_raw_buffer_clone(anjay_raw_buffer_t *dst,
                            const anjay_raw_buffer_t *src) {
    return _anjay_raw_buffer_from_data(dst, src->data, src->size);
}

int _anjay_raw_buffer_alloc(anjay_raw_buffer_t *dst, size_t capacity) {
    assert(!dst->data && !dst->size);
    if (!capacity) {
        return 0;
    }
    dst->data = avs_malloc(capacity);
    if (!dst->data) {
        return -1;
    }
    dst->capacity = capacity;
    return 0;
}

int _anjay_raw_buffer_from_data(anjay_raw_buffer_t *dst,
                                const void *src,
                                size_t size) {
    int result = _anjay_raw_buffer_alloc(dst, size);
    if (!result) {
        dst->size = size;
        memcpy(dst->data, src, size);
    }
    return result;
}
