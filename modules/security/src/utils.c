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

#include "utils.h"

#include <string.h>

VISIBILITY_SOURCE_BEGIN

typedef int chunk_getter_t(anjay_input_ctx_t *ctx,
                           char *out,
                           size_t out_size,
                           bool *out_finished,
                           size_t *out_bytes_read);

static int _anjay_sec_bytes_getter(anjay_input_ctx_t *ctx,
                                   char *out,
                                   size_t size,
                                   bool *out_finished,
                                   size_t *out_bytes_read) {
    return anjay_get_bytes(ctx, out_bytes_read, out_finished, out, size);
}

static int _anjay_sec_string_getter(anjay_input_ctx_t *ctx,
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

static int _anjay_sec_generic_getter(anjay_input_ctx_t *ctx,
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
                    (char *) realloc(buffer, buffer_size + chunk_bytes_read);
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
    free(buffer);
    return result;
}

int _anjay_sec_fetch_bytes(anjay_input_ctx_t *ctx, anjay_raw_buffer_t *buffer) {
    _anjay_raw_buffer_clear(buffer);
    int retval =
            _anjay_sec_generic_getter(ctx, (char **) &buffer->data,
                                      &buffer->size, _anjay_sec_bytes_getter);
    buffer->capacity = buffer->size;
    return retval;
}

int _anjay_sec_fetch_string(anjay_input_ctx_t *ctx, char **out) {
    free(*out);
    *out = NULL;
    size_t bytes_read = 0;
    return _anjay_sec_generic_getter(ctx, out, &bytes_read,
                                     _anjay_sec_string_getter);
}

int _anjay_sec_validate_security_mode(int32_t security_mode) {
    switch (security_mode) {
    case ANJAY_UDP_SECURITY_NOSEC:
    case ANJAY_UDP_SECURITY_PSK:
    case ANJAY_UDP_SECURITY_CERTIFICATE:
        return 0;
    case ANJAY_UDP_SECURITY_RPK:
        security_log(ERROR, "Raw Public Key mode not supported");
        return ANJAY_ERR_NOT_IMPLEMENTED;
    default:
        security_log(ERROR, "Invalid Security Mode");
        return ANJAY_ERR_BAD_REQUEST;
    }
}

int _anjay_sec_fetch_security_mode(anjay_input_ctx_t *ctx,
                                    anjay_udp_security_mode_t *out) {
    int32_t value;
    int retval = anjay_get_i32(ctx, &value);
    if (!retval) {
        retval = _anjay_sec_validate_security_mode(value);
    }
    if (!retval) {
        *out = (anjay_udp_security_mode_t) value;
    }
    return retval;
}

static int _anjay_sec_validate_short_server_id(int32_t ssid) {
    return ssid > 0 && ssid <= UINT16_MAX ? 0 : -1;
}

int _anjay_sec_fetch_short_server_id(anjay_input_ctx_t *ctx,
                                     anjay_ssid_t *out) {
    int32_t value;
    int retval = anjay_get_i32(ctx, &value);
    if (!retval) {
        retval = _anjay_sec_validate_short_server_id(value);
    }
    if (!retval) {
        *out = (anjay_ssid_t) value;
    }
    return retval;
}

void _anjay_sec_destroy_instance_fields(sec_instance_t *instance) {
    if (!instance) {
        return;
    }
    free((char *) (intptr_t) instance->server_uri);
    _anjay_raw_buffer_clear(&instance->public_cert_or_psk_identity);
    _anjay_raw_buffer_clear(&instance->private_cert_or_psk_key);
    _anjay_raw_buffer_clear(&instance->server_public_key);
}

void _anjay_sec_destroy_instances(AVS_LIST(sec_instance_t) *instances_ptr) {
    AVS_LIST_CLEAR(instances_ptr) {
        _anjay_sec_destroy_instance_fields(*instances_ptr);
    }
}

static int _anjay_sec_clone_instance(sec_instance_t *dest,
                                     const sec_instance_t *src) {
    *dest = *src;
    dest->public_cert_or_psk_identity = ANJAY_RAW_BUFFER_EMPTY;
    dest->private_cert_or_psk_key = ANJAY_RAW_BUFFER_EMPTY;
    dest->server_public_key = ANJAY_RAW_BUFFER_EMPTY;
    dest->server_uri = NULL;

    dest->server_uri = strdup(src->server_uri);
    if (!dest->server_uri) {
        security_log(ERROR, "Cannot clone Server Uri resource");
        return -1;
    }
    if (_anjay_raw_buffer_clone(&dest->public_cert_or_psk_identity,
                                &src->public_cert_or_psk_identity)) {
        security_log(ERROR, "Cannot clone Pk Or Identity resource");
        return -1;
    }
    if (_anjay_raw_buffer_clone(&dest->private_cert_or_psk_key,
                                &src->private_cert_or_psk_key)) {
        security_log(ERROR, "Cannot clone Secret Key resource");
        return -1;
    }
    if (_anjay_raw_buffer_clone(&dest->server_public_key,
                                &src->server_public_key)) {
        security_log(ERROR, "Cannot clone Server Public Key resource");
        return -1;
    }
    return 0;
}

AVS_LIST(sec_instance_t) _anjay_sec_clone_instances(const sec_repr_t *repr) {
    AVS_LIST(sec_instance_t) retval = NULL;
    AVS_LIST(sec_instance_t) current;
    AVS_LIST(sec_instance_t) *last;
    last = &retval;

    AVS_LIST_FOREACH(current, repr->instances) {
        if (AVS_LIST_INSERT_NEW(sec_instance_t, last)) {
            if (_anjay_sec_clone_instance(*last, current)) {
                security_log(ERROR, "Cannot clone Security Object Instances");
                _anjay_sec_destroy_instances(&retval);
                return NULL;
            }
            last = AVS_LIST_NEXT_PTR(last);
        }
    }
    return retval;
}
