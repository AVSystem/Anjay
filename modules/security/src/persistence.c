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

#include <anjay/persistence.h>

#include <string.h>

#include "security.h"
#include "transaction.h"
#include "utils.h"

VISIBILITY_SOURCE_BEGIN

#define persistence_log(level, ...) \
    _anjay_log(security_persistence, level, __VA_ARGS__)

static const char MAGIC_V0[] = { 'S', 'E', 'C', '\0' };
static const char MAGIC_V1[] = { 'S', 'E', 'C', '\1' };

static int handle_sized_v0_fields(anjay_persistence_context_t *ctx,
                                  sec_instance_t *element) {
    int retval;
    (void) ((retval = anjay_persistence_u16(ctx, &element->iid))
            || (retval = anjay_persistence_bool(ctx,
                                                &element->has_is_bootstrap))
            || (retval = anjay_persistence_bool(ctx,
                                                &element->has_udp_security_mode))
            || (retval = anjay_persistence_bool(ctx, &element->has_ssid))
            || (retval = anjay_persistence_bool(ctx,
                                                &element->is_bootstrap))
            || (retval = anjay_persistence_u16(ctx, &element->ssid))
            || (retval = anjay_persistence_u32(ctx, (uint32_t *) &element->holdoff_s))
            || (retval = anjay_persistence_u32(ctx, (uint32_t *) &element->bs_timeout_s)));
    return retval;
}

static int handle_sized_v1_fields(anjay_persistence_context_t *ctx,
                                  sec_instance_t *element) {
    int retval;
    (void) ((retval = anjay_persistence_bool(ctx,
                                             &element->has_sms_security_mode))
            || (retval = anjay_persistence_bool(ctx,
                                                &element->has_sms_key_params))
            || (retval = anjay_persistence_bool(ctx,
                                                &element->has_sms_secret_key)));
    return retval;
}

static int store_raw_buffer(anjay_persistence_context_t *ctx,
                            const anjay_raw_buffer_t *buffer) {
    assert(buffer->size == (uint32_t) buffer->size);
    uint32_t size = (uint32_t) buffer->size;
    int retval = anjay_persistence_u32(ctx, &size);
    if (!retval && size > 0) {
        retval = anjay_persistence_bytes(ctx, (uint8_t *) buffer->data, size);
    }
    return retval;
}

static int restore_raw_buffer(anjay_persistence_context_t *ctx,
                              anjay_raw_buffer_t *buffer) {
    uint32_t size;
    int retval = anjay_persistence_u32(ctx, &size);
    if (retval) {
        return retval;
    }
    assert(!buffer->data);
    assert(!buffer->size);
    assert(!buffer->capacity);
    if (size == 0) {
        return 0;
    }
    buffer->data = malloc(size);
    if (!buffer->data) {
        persistence_log(ERROR, "Cannot allocate %u bytes", size);
        return -1;
    }
    buffer->size = size;
    buffer->capacity = size;
    if ((retval = anjay_persistence_bytes(ctx, (uint8_t *) buffer->data,
                                          buffer->size))) {
        _anjay_raw_buffer_clear(buffer);
    }
    return retval;
}

static int store_string(anjay_persistence_context_t *ctx,
                        const char *str) {
    const size_t size = str ? (strlen(str) + 1) : 0;
    const anjay_raw_buffer_t buffer = {
        .data = (void *) (intptr_t) str,
        .size = size,
        .capacity = size
    };
    return store_raw_buffer(ctx, &buffer);
}

static int restore_string(anjay_persistence_context_t *ctx,
                          char **out_str) {
    anjay_raw_buffer_t buffer = ANJAY_RAW_BUFFER_EMPTY;
    int retval = restore_raw_buffer(ctx, &buffer);
    if (retval) {
        return retval;
    }
    const char *str = (char *) buffer.data;
    if (buffer.size > 0 && str[buffer.size-1] != '\0') {
        persistence_log(ERROR, "Invalid string");
        _anjay_raw_buffer_clear(&buffer);
        return -1;
    }
    *out_str = (char *) buffer.data;
    return 0;
}

static int store_instance(anjay_persistence_context_t *ctx,
                          void *element_,
                          void *user_data) {
    (void) user_data;
    sec_instance_t *element = (sec_instance_t *) element_;
    int retval = 0;
    uint16_t udp_security_mode = (uint16_t) element->udp_security_mode;
    uint16_t sms_security_mode = (uint16_t) element->sms_security_mode;
    (void) ((retval = handle_sized_v0_fields(ctx, element))
                || (retval = anjay_persistence_u16(ctx, &udp_security_mode))
                || (retval = store_string(ctx, element->server_uri))
                || (retval = store_raw_buffer(
                        ctx, &element->public_cert_or_psk_identity))
                || (retval = store_raw_buffer(
                        ctx, &element->private_cert_or_psk_key))
                || (retval = store_raw_buffer(
                        ctx, &element->server_public_key))
                || (retval = handle_sized_v1_fields(ctx, element))
                || (retval = anjay_persistence_u16(ctx, &sms_security_mode))
                || (retval = store_raw_buffer(ctx, &element->sms_key_params))
                || (retval = store_raw_buffer(ctx, &element->sms_secret_key))
                || (retval = store_string(ctx, element->sms_number)));
    return retval;
}

static int restore_instance(anjay_persistence_context_t *ctx,
                            sec_instance_t *element,
                            int stream_version) {
    int retval = 0;
    uint16_t udp_security_mode;
    if ((retval = handle_sized_v0_fields(ctx, element))
            || (retval = anjay_persistence_u16(ctx, &udp_security_mode))
            || (retval = restore_string(ctx, &element->server_uri))
            || (retval = restore_raw_buffer(
                    ctx, &element->public_cert_or_psk_identity))
            || (retval = restore_raw_buffer(ctx,
                                            &element->private_cert_or_psk_key))
            || (retval = restore_raw_buffer(ctx,
                                            &element->server_public_key))) {
        return retval;
    }
    element->udp_security_mode = (anjay_udp_security_mode_t) udp_security_mode;
    if (stream_version >= 1) {
        uint16_t sms_security_mode;
        if ((retval = handle_sized_v1_fields(ctx, element))
                || (retval = anjay_persistence_u16(ctx, &sms_security_mode))
                || (retval = restore_raw_buffer(ctx, &element->sms_key_params))
                || (retval = restore_raw_buffer(ctx, &element->sms_secret_key))
                || (retval = restore_string(ctx, &element->sms_number))) {
            return retval;
        }
        element->sms_security_mode =
                (anjay_sms_security_mode_t) sms_security_mode;
    }
    return 0;
}

static int restore_instances(anjay_persistence_context_t *ctx,
                             AVS_LIST(sec_instance_t) *instances,
                             int stream_version) {
    uint32_t count;
    int retval = anjay_persistence_u32(ctx, &count);
    if (retval || count > UINT16_MAX) {
        persistence_log(ERROR, "Cannot read number of instances to restore");
        return retval;
    }

    AVS_LIST(sec_instance_t) *tail = instances;
    while (count--) {
        if (!AVS_LIST_INSERT_NEW(sec_instance_t, tail)) {
            persistence_log(ERROR, "Out of memory");
            return -1;
        }
        if ((retval = restore_instance(ctx, *tail, stream_version))) {
            return retval;
        }
        tail = AVS_LIST_NEXT_PTR(tail);
    }
    return 0;
}

int anjay_security_object_persist(const anjay_dm_object_def_t *const *obj,
                                  avs_stream_abstract_t *out_stream) {
    sec_repr_t *repr = _anjay_sec_get(obj);
    if (!repr) {
        return -1;
    }
    int retval = avs_stream_write(out_stream, MAGIC_V1, sizeof(MAGIC_V1));
    if (retval) {
        return retval;
    }
    anjay_persistence_context_t *ctx =
            anjay_persistence_store_context_new(out_stream);
    if (!ctx) {
        persistence_log(ERROR, "Out of memory");
        return -1;
    }
    retval = anjay_persistence_list(ctx, (AVS_LIST(void) *) &repr->instances,
                                    sizeof(sec_instance_t),
                                    store_instance, NULL);
    anjay_persistence_context_delete(ctx);
    return retval;
}

int anjay_security_object_restore(const anjay_dm_object_def_t *const *obj,
                                  avs_stream_abstract_t *in_stream) {
    sec_repr_t *repr = _anjay_sec_get(obj);
    if (!repr) {
        return -1;
    }
    sec_repr_t backup = *repr;

    AVS_STATIC_ASSERT(sizeof(MAGIC_V0) == sizeof(MAGIC_V1), magic_size);
    char magic_header[sizeof(MAGIC_V1)];
    int retval = avs_stream_read_reliably(in_stream, magic_header,
                                          sizeof(magic_header));
    if (retval) {
        persistence_log(ERROR, "Could not read Security Object header");
        return retval;
    }

    int version;
    if (!memcmp(magic_header, MAGIC_V0, sizeof(MAGIC_V0))) {
        version = 0;
    } else if (!memcmp(magic_header, MAGIC_V1, sizeof(MAGIC_V1))) {
        version = 1;
    } else {
        persistence_log(ERROR, "Header magic constant mismatch");
        return -1;
    }
    anjay_persistence_context_t *restore_ctx =
            anjay_persistence_restore_context_new(in_stream);
    if (!restore_ctx) {
        persistence_log(ERROR, "Cannot create persistence restore context");
        return -1;
    }
    repr->instances = NULL;
    retval = restore_instances(restore_ctx, &repr->instances, version);
    if (retval || (retval = _anjay_sec_object_validate(repr))) {
        _anjay_sec_destroy_instances(&repr->instances);
        repr->instances = backup.instances;
    } else {
        _anjay_sec_destroy_instances(&backup.instances);
    }
    anjay_persistence_context_delete(restore_ctx);
    return retval;
}

#ifdef ANJAY_TEST
#include "test/persistence.c"
#endif
