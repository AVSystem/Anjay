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

#include <anjay_init.h>

#include <string.h>

#include <anjay_modules/anjay_io_utils.h>

VISIBILITY_SOURCE_BEGIN

typedef int chunk_getter_t(anjay_input_ctx_t *ctx,
                           char *out,
                           size_t out_size,
                           bool *out_finished,
                           size_t *out_bytes_read);

static int bytes_getter(anjay_input_ctx_t *ctx,
                        char *out,
                        size_t size,
                        bool *out_finished,
                        size_t *out_bytes_read) {
    return anjay_get_bytes(ctx, out_bytes_read, out_finished, out, size);
}

static int string_getter(anjay_input_ctx_t *ctx,
                         char *out,
                         size_t size,
                         bool *out_finished,
                         size_t *out_bytes_read) {
    int result = anjay_get_string(ctx, out, size);
    if (result < 0) {
        return result;
    }
    *out_finished = true;
    *out_bytes_read = strlen(out) + 1;
    if (result == ANJAY_BUFFER_TOO_SHORT) {
        *out_finished = false;
        /**
         * We don't want null terminator, because we're still in the phase of
         * string chunk concatenation (and null terminators in the middle of
         * the string are rather bad).
         */
        --*out_bytes_read;
    }
    return 0;
}

static int generic_getter(anjay_input_ctx_t *ctx,
                          char **out,
                          size_t *out_bytes_read,
                          chunk_getter_t *getter) {
    char tmp[128];
    bool finished = false;
    char *buffer = NULL;
    size_t buffer_size = 0;
    int result;
    do {
        size_t chunk_bytes_read = 0;
        if ((result = getter(ctx, tmp, sizeof(tmp), &finished,
                             &chunk_bytes_read))) {
            goto error;
        }
        if (chunk_bytes_read > 0) {
            char *bigger_buffer =
                    (char *) avs_realloc(buffer,
                                         buffer_size + chunk_bytes_read);
            if (!bigger_buffer) {
                result = ANJAY_ERR_INTERNAL;
                goto error;
            }
            memcpy(bigger_buffer + buffer_size, tmp, chunk_bytes_read);
            buffer = bigger_buffer;
            buffer_size += chunk_bytes_read;
        }
    } while (!finished);
    *out = buffer;
    *out_bytes_read = buffer_size;
    return 0;
error:
    avs_free(buffer);
    return result;
}

int _anjay_io_fetch_bytes(anjay_input_ctx_t *ctx, anjay_raw_buffer_t *buffer) {
    _anjay_raw_buffer_clear(buffer);
    int retval = generic_getter(ctx, (char **) &buffer->data, &buffer->size,
                                bytes_getter);
    buffer->capacity = buffer->size;
    return retval;
}

int _anjay_io_fetch_string(anjay_input_ctx_t *ctx, char **out) {
    avs_free(*out);
    *out = NULL;
    size_t bytes_read = 0;
    return generic_getter(ctx, out, &bytes_read, string_getter);
}
