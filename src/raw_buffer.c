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

#include <config.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <anjay_modules/utils.h>

VISIBILITY_SOURCE_BEGIN

void _anjay_raw_buffer_clear(anjay_raw_buffer_t *buffer) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

int _anjay_raw_buffer_clone(anjay_raw_buffer_t *dst,
                            const anjay_raw_buffer_t *src) {
    return _anjay_raw_buffer_from_data(dst, src->data, src->size);
}

int _anjay_raw_buffer_from_data(anjay_raw_buffer_t *dst,
                                const void *src,
                                size_t size) {
    assert(!dst->data && !dst->size);
    if (!size) {
        return 0;
    }
    dst->data = malloc(size);
    if (!dst->data) {
        return -1;
    }
    dst->size = size;
    dst->capacity = size;
    memcpy(dst->data, src, size);
    return 0;
}
