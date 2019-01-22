/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <anjay_config.h>

#ifdef WITH_AVS_PERSISTENCE
#    include <avsystem/commons/persistence.h>
#endif // WITH_AVS_PERSISTENCE

#include <anjay_modules/dm_utils.h>

#include <inttypes.h>
#include <string.h>

#include "mod_security.h"
#include "security_transaction.h"
#include "security_utils.h"

VISIBILITY_SOURCE_BEGIN

#define persistence_log(level, ...) \
    _anjay_log(security_persistence, level, __VA_ARGS__)

#ifdef WITH_AVS_PERSISTENCE

static const char MAGIC_V0[] = { 'S', 'E', 'C', '\0' };
static const char MAGIC_V1[] = { 'S', 'E', 'C', '\1' };

static int handle_sized_v0_fields(avs_persistence_context_t *ctx,
                                  sec_instance_t *element) {
    int retval;
    (void) ((retval = avs_persistence_u16(ctx, &element->iid))
            || (retval = avs_persistence_bool(ctx, &element->has_is_bootstrap))
            || (retval = avs_persistence_bool(ctx,
                                              &element->has_udp_security_mode))
            || (retval = avs_persistence_bool(ctx, &element->has_ssid))
            || (retval = avs_persistence_bool(ctx, &element->is_bootstrap))
            || (retval = avs_persistence_u16(ctx, &element->ssid))
            || (retval = avs_persistence_u32(ctx,
                                             (uint32_t *) &element->holdoff_s))
            || (retval = avs_persistence_u32(
                        ctx, (uint32_t *) &element->bs_timeout_s)));
    return retval;
}

static int handle_sized_v1_fields(avs_persistence_context_t *ctx,
                                  sec_instance_t *element) {
    int retval;
    (void) ((retval =
                     avs_persistence_bool(ctx, &element->has_sms_security_mode))
            || (retval =
                        avs_persistence_bool(ctx, &element->has_sms_key_params))
            || (retval = avs_persistence_bool(ctx,
                                              &element->has_sms_secret_key)));
    return retval;
}

static int handle_raw_buffer(avs_persistence_context_t *ctx,
                             anjay_raw_buffer_t *buffer) {
    int retval =
            avs_persistence_sized_buffer(ctx, &buffer->data, &buffer->size);
    if (!buffer->capacity) {
        buffer->capacity = buffer->size;
    }
    return retval;
}

static int handle_instance(avs_persistence_context_t *ctx,
                           void *element_,
                           void *stream_version_) {
    sec_instance_t *element = (sec_instance_t *) element_;
    const intptr_t stream_version = (intptr_t) stream_version_;
    int retval = 0;
    uint16_t udp_security_mode = (uint16_t) element->udp_security_mode;
    if ((retval = handle_sized_v0_fields(ctx, element))
            || (retval = avs_persistence_u16(ctx, &udp_security_mode))
            || (retval = avs_persistence_string(ctx, &element->server_uri))
            || (retval = handle_raw_buffer(
                        ctx, &element->public_cert_or_psk_identity))
            || (retval = handle_raw_buffer(ctx,
                                           &element->private_cert_or_psk_key))
            || (retval = handle_raw_buffer(ctx, &element->server_public_key))) {
        return retval;
    }
    element->udp_security_mode = (anjay_udp_security_mode_t) udp_security_mode;
    if (stream_version >= 1) {
        uint16_t sms_security_mode = (uint16_t) element->sms_security_mode;
        if ((retval = handle_sized_v1_fields(ctx, element))
                || (retval = avs_persistence_u16(ctx, &sms_security_mode))
                || (retval = handle_raw_buffer(ctx, &element->sms_key_params))
                || (retval = handle_raw_buffer(ctx, &element->sms_secret_key))
                || (retval = avs_persistence_string(ctx,
                                                    &element->sms_number))) {
            return retval;
        }
        element->sms_security_mode =
                (anjay_sms_security_mode_t) sms_security_mode;
    }
    return 0;
}

int anjay_security_object_persist(anjay_t *anjay,
                                  avs_stream_abstract_t *out_stream) {
    assert(anjay);

    const anjay_dm_object_def_t *const *sec_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    sec_repr_t *repr = _anjay_sec_get(sec_obj);
    if (!repr) {
        return -1;
    }
    int retval = avs_stream_write(out_stream, MAGIC_V1, sizeof(MAGIC_V1));
    if (retval) {
        return retval;
    }
    avs_persistence_context_t *ctx =
            avs_persistence_store_context_new(out_stream);
    if (!ctx) {
        persistence_log(ERROR, "Out of memory");
        return -1;
    }
    retval = avs_persistence_list(ctx, (AVS_LIST(void) *) &repr->instances,
                                  sizeof(sec_instance_t), handle_instance,
                                  (void *) (intptr_t) 1, NULL);
    avs_persistence_context_delete(ctx);
    if (!retval) {
        _anjay_sec_clear_modified(repr);
        persistence_log(INFO, "Security Object state persisted");
    }
    return retval;
}

int anjay_security_object_restore(anjay_t *anjay,
                                  avs_stream_abstract_t *in_stream) {
    assert(anjay);

    const anjay_dm_object_def_t *const *sec_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SECURITY);
    sec_repr_t *repr = _anjay_sec_get(sec_obj);
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
    avs_persistence_context_t *restore_ctx =
            avs_persistence_restore_context_new(in_stream);
    if (!restore_ctx) {
        persistence_log(ERROR, "Cannot create persistence restore context");
        return -1;
    }
    repr->instances = NULL;
    retval = avs_persistence_list(restore_ctx,
                                  (AVS_LIST(void) *) &repr->instances,
                                  sizeof(sec_instance_t), handle_instance,
                                  (void *) (intptr_t) version, NULL);
    if (retval || (retval = _anjay_sec_object_validate(repr))) {
        _anjay_sec_destroy_instances(&repr->instances);
        repr->instances = backup.instances;
    } else {
        _anjay_sec_destroy_instances(&backup.instances);
    }
    avs_persistence_context_delete(restore_ctx);
    if (!retval) {
        _anjay_sec_clear_modified(repr);
        persistence_log(INFO, "Security Object state restored");
    }
    return retval;
}

#    ifdef ANJAY_TEST
#        include "test/persistence.c"
#    endif

#else // WITH_AVS_PERSISTENCE

int anjay_security_object_persist(anjay_t *anjay,
                                  avs_stream_abstract_t *out_stream) {
    (void) anjay;
    (void) out_stream;
    persistence_log(ERROR, "Persistence not compiled in");
    return -1;
}

int anjay_security_object_restore(anjay_t *anjay,
                                  avs_stream_abstract_t *in_stream) {
    (void) anjay;
    (void) in_stream;
    persistence_log(ERROR, "Persistence not compiled in");
    return -1;
}

#endif // WITH_AVS_PERSISTENCE
