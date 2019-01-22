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
#include <avsystem/commons/utils.h>

#include <anjay_modules/dm_utils.h>
#include <anjay_modules/utils_core.h>

#include <inttypes.h>
#include <string.h>

#include "mod_server.h"
#include "server_transaction.h"
#include "server_utils.h"

VISIBILITY_SOURCE_BEGIN

#define persistence_log(level, ...) \
    _anjay_log(server_persistence, level, __VA_ARGS__)

#ifdef WITH_AVS_PERSISTENCE

static const char MAGIC[] = { 'S', 'R', 'V', '\0' };

static int handle_sized_fields(avs_persistence_context_t *ctx, void *element_) {
    server_instance_t *element = (server_instance_t *) element_;
    bool has_binding;
    int retval = 0;
    if (avs_persistence_direction(ctx) == AVS_PERSISTENCE_STORE) {
        has_binding = !!element->data.binding;
    }
    (void) ((retval = avs_persistence_u16(ctx, &element->iid))
            || (retval = avs_persistence_bool(ctx, &element->has_ssid))
            || (retval = avs_persistence_bool(ctx, &has_binding))
            || (retval = avs_persistence_bool(ctx, &element->has_lifetime))
            || (retval = avs_persistence_bool(
                        ctx, &element->has_notification_storing))
            || (retval = avs_persistence_u16(ctx, &element->data.ssid))
            || (retval = avs_persistence_u32(
                        ctx, (uint32_t *) &element->data.lifetime))
            || (retval = avs_persistence_u32(
                        ctx, (uint32_t *) &element->data.default_min_period))
            || (retval = avs_persistence_u32(
                        ctx, (uint32_t *) &element->data.default_max_period))
            || (retval = avs_persistence_u32(
                        ctx, (uint32_t *) &element->data.disable_timeout))
            || (retval = avs_persistence_bool(
                        ctx, &element->data.notification_storing)));
    if (!retval && avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE) {
        element->data.binding = has_binding ? element->binding_buf : NULL;
    }
    return retval;
}

typedef enum {
    LEGACY_BINDING_NONE,
    LEGACY_BINDING_U,
    LEGACY_BINDING_UQ,
    LEGACY_BINDING_S,
    LEGACY_BINDING_SQ,
    LEGACY_BINDING_US,
    LEGACY_BINDING_UQS
} legacy_binding_mode_t;

static int persist_instance(avs_persistence_context_t *ctx,
                            void *element_,
                            void *user_data) {
    (void) user_data;
    server_instance_t *element = (server_instance_t *) element_;
    int retval = 0;
    uint32_t binding;
    if (!element->data.binding) {
        binding = (uint32_t) LEGACY_BINDING_NONE;
    } else if (strcmp(element->data.binding, "U") == 0) {
        binding = (uint32_t) LEGACY_BINDING_U;
    } else if (strcmp(element->data.binding, "UQ") == 0) {
        binding = (uint32_t) LEGACY_BINDING_UQ;
    } else if (strcmp(element->data.binding, "S") == 0) {
        binding = (uint32_t) LEGACY_BINDING_S;
    } else if (strcmp(element->data.binding, "SQ") == 0) {
        binding = (uint32_t) LEGACY_BINDING_SQ;
    } else if (strcmp(element->data.binding, "US") == 0) {
        binding = (uint32_t) LEGACY_BINDING_US;
    } else if (strcmp(element->data.binding, "UQS") == 0) {
        binding = (uint32_t) LEGACY_BINDING_UQS;
    } else {
        return -1;
    }
    (void) ((retval = handle_sized_fields(ctx, element_))
            || (retval = avs_persistence_u32(ctx, &binding)));
    return retval;
}

static int restore_instance(avs_persistence_context_t *ctx,
                            void *element_,
                            void *user_data) {
    (void) user_data;
    server_instance_t *element = (server_instance_t *) element_;
    int retval = 0;
    uint32_t binding;
    (void) ((retval = handle_sized_fields(ctx, element_))
            || (retval = avs_persistence_u32(ctx, &binding)));
    if (!retval) {
        const char *binding_str;
        switch (binding) {
        case LEGACY_BINDING_NONE:
            binding_str = "";
            break;
        case LEGACY_BINDING_U:
            binding_str = "U";
            break;
        case LEGACY_BINDING_UQ:
            binding_str = "UQ";
            break;
        case LEGACY_BINDING_S:
            binding_str = "S";
            break;
        case LEGACY_BINDING_SQ:
            binding_str = "SQ";
            break;
        case LEGACY_BINDING_US:
            binding_str = "US";
            break;
        case LEGACY_BINDING_UQS:
            binding_str = "UQS";
            break;
        default:
            persistence_log(ERROR, "Invalid binding mode: %" PRIu32, binding);
            retval = -1;
            break;
        }
        if (!retval
                && avs_simple_snprintf(element->binding_buf,
                                       sizeof(element->binding_buf), "%s",
                                       binding_str)
                               < 0) {
            persistence_log(ERROR, "Could not restore binding: %s",
                            binding_str);
            retval = -1;
        }
    }
    return retval;
}

int anjay_server_object_persist(anjay_t *anjay,
                                avs_stream_abstract_t *out_stream) {
    assert(anjay);

    const anjay_dm_object_def_t *const *server_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SERVER);
    server_repr_t *repr = _anjay_serv_get(server_obj);
    if (!repr) {
        return -1;
    }
    int retval = avs_stream_write(out_stream, MAGIC, sizeof(MAGIC));
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
                                  sizeof(server_instance_t), persist_instance,
                                  NULL, NULL);
    avs_persistence_context_delete(ctx);
    if (!retval) {
        _anjay_serv_clear_modified(repr);
        persistence_log(INFO, "Server Object state persisted");
    }
    return retval;
}

int anjay_server_object_restore(anjay_t *anjay,
                                avs_stream_abstract_t *in_stream) {
    assert(anjay);

    const anjay_dm_object_def_t *const *server_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SERVER);
    server_repr_t *repr = _anjay_serv_get(server_obj);
    if (!repr) {
        return -1;
    }
    server_repr_t backup = *repr;

    char magic_header[sizeof(MAGIC)];
    int retval = avs_stream_read_reliably(in_stream, magic_header,
                                          sizeof(magic_header));
    if (retval) {
        persistence_log(ERROR, "Could not read Server Object header");
        return retval;
    }

    if (memcmp(magic_header, MAGIC, sizeof(MAGIC))) {
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
                                  sizeof(server_instance_t), restore_instance,
                                  NULL, NULL);
    if (retval || (retval = _anjay_serv_object_validate(repr))) {
        _anjay_serv_destroy_instances(&repr->instances);
        repr->instances = backup.instances;
    } else {
        _anjay_serv_destroy_instances(&backup.instances);
    }
    avs_persistence_context_delete(restore_ctx);
    if (!retval) {
        _anjay_serv_clear_modified(repr);
        persistence_log(INFO, "Server Object state restored");
    }
    return retval;
}

#    ifdef ANJAY_TEST
#        include "test/persistence.c"
#    endif

#else // WITH_AVS_PERSISTENCE

int anjay_server_object_persist(anjay_t *anjay,
                                avs_stream_abstract_t *out_stream) {
    (void) anjay;
    (void) out_stream;
    persistence_log(ERROR, "Persistence not compiled in");
    return -1;
}

int anjay_server_object_restore(anjay_t *anjay,
                                avs_stream_abstract_t *in_stream) {
    (void) anjay;
    (void) in_stream;
    persistence_log(ERROR, "Persistence not compiled in");
    return -1;
}

#endif // WITH_AVS_PERSISTENCE
