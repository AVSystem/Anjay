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

typedef enum {
    PERSISTENCE_VERSION_0,
    PERSISTENCE_VERSION_1
} server_persistence_version_t;

typedef char magic_t[4];
static const magic_t MAGIC_V0 = { 'S', 'R', 'V', PERSISTENCE_VERSION_0 };
static const magic_t MAGIC_V1 = { 'S', 'R', 'V', PERSISTENCE_VERSION_1 };

static avs_error_t handle_sized_fields(avs_persistence_context_t *ctx,
                                       server_instance_t *element) {
    avs_error_t err;
    bool has_binding = !!element->data.binding;
    (void) (avs_is_err((err = avs_persistence_u16(ctx, &element->iid)))
            || avs_is_err((err = avs_persistence_bool(ctx, &element->has_ssid)))
            || avs_is_err((err = avs_persistence_bool(ctx, &has_binding)))
            || avs_is_err((
                       err = avs_persistence_bool(ctx, &element->has_lifetime)))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, &element->has_notification_storing)))
            || avs_is_err((err = avs_persistence_u16(ctx, &element->data.ssid)))
            || avs_is_err((err = avs_persistence_u32(
                                   ctx, (uint32_t *) &element->data.lifetime)))
            || avs_is_err((
                       err = avs_persistence_u32(
                               ctx,
                               (uint32_t *) &element->data.default_min_period)))
            || avs_is_err((
                       err = avs_persistence_u32(
                               ctx,
                               (uint32_t *) &element->data.default_max_period)))
            || avs_is_err(
                       (err = avs_persistence_u32(
                                ctx,
                                (uint32_t *) &element->data.disable_timeout)))
            || avs_is_err((err = avs_persistence_bool(
                                   ctx, &element->data.notification_storing))));
    if (avs_is_ok(err)
            && avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE) {
        element->data.binding = has_binding ? element->binding_buf : NULL;
    }
    return err;
}

static avs_error_t handle_binding_mode(avs_persistence_context_t *ctx,
                                       server_instance_t *element) {
    avs_error_t err = avs_persistence_bytes(ctx, element->binding_buf,
                                            sizeof(element->binding_buf));
    if (avs_is_err(err)) {
        return err;
    }
    if (!memchr(element->binding_buf, '\0', sizeof(element->binding_buf))
            || !anjay_binding_mode_valid(element->binding_buf)) {
        return avs_errno(AVS_EBADMSG);
    }
    return AVS_OK;
}

static avs_error_t restore_legacy_binding_mode(avs_persistence_context_t *ctx,
                                               server_instance_t *element) {
    assert(avs_persistence_direction(ctx) == AVS_PERSISTENCE_RESTORE);
    uint32_t binding;
    avs_error_t err = avs_persistence_u32(ctx, &binding);
    if (avs_is_err(err)) {
        return err;
    }

    enum {
        LEGACY_BINDING_NONE,
        LEGACY_BINDING_U,
        LEGACY_BINDING_UQ,
        LEGACY_BINDING_S,
        LEGACY_BINDING_SQ,
        LEGACY_BINDING_US,
        LEGACY_BINDING_UQS
    };

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
        persistence_log(WARNING, "Invalid binding mode: %" PRIu32, binding);
        err = avs_errno(AVS_EBADMSG);
        break;
    }
    if (avs_is_ok(err)
            && avs_simple_snprintf(element->binding_buf,
                                   sizeof(element->binding_buf), "%s",
                                   binding_str)
                           < 0) {
        persistence_log(WARNING, "Could not restore binding: %s", binding_str);
        err = avs_errno(AVS_EBADMSG);
    }
    return err;
}

static avs_error_t server_instance_persistence_handler(
        avs_persistence_context_t *ctx, void *element_, void *version_) {
    server_instance_t *element = (server_instance_t *) element_;
    server_persistence_version_t *version =
            (server_persistence_version_t *) version_;
    AVS_ASSERT(avs_persistence_direction(ctx) != AVS_PERSISTENCE_STORE
                       || *version == PERSISTENCE_VERSION_1,
               "persistence storing is impossible in legacy mode");

    avs_error_t err;
    switch (*version) {
    case PERSISTENCE_VERSION_0:
        (void) (avs_is_err((err = handle_sized_fields(ctx, element)))
                || avs_is_err(
                           (err = restore_legacy_binding_mode(ctx, element))));
        break;
    case PERSISTENCE_VERSION_1:
        (void) (avs_is_err((err = handle_sized_fields(ctx, element)))
                || avs_is_err((err = handle_binding_mode(ctx, element))));
        break;
    default:
        AVS_UNREACHABLE("invalid enum value");
    }
    return err;
}

avs_error_t anjay_server_object_persist(anjay_t *anjay,
                                        avs_stream_t *out_stream) {
    assert(anjay);

    const anjay_dm_object_def_t *const *server_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SERVER);
    server_repr_t *repr = _anjay_serv_get(server_obj);
    if (!repr) {
        return avs_errno(AVS_EBADF);
    }
    avs_persistence_context_t persist_ctx =
            avs_persistence_store_context_create(out_stream);

    avs_error_t err =
            avs_persistence_bytes(&persist_ctx, (void *) (intptr_t) MAGIC_V1,
                                  sizeof(MAGIC_V1));
    if (avs_is_err(err)) {
        return err;
    }
    server_persistence_version_t persistence_version = PERSISTENCE_VERSION_1;
    err = avs_persistence_list(&persist_ctx,
                               (AVS_LIST(void) *) &repr->instances,
                               sizeof(server_instance_t),
                               server_instance_persistence_handler,
                               &persistence_version, NULL);
    if (avs_is_ok(err)) {
        _anjay_serv_clear_modified(repr);
        persistence_log(INFO, "Server Object state persisted");
    }
    return err;
}

static int check_magic_header(magic_t magic_header,
                              server_persistence_version_t *out_version) {
    if (!memcmp(magic_header, MAGIC_V0, sizeof(magic_t))) {
        *out_version = PERSISTENCE_VERSION_0;
        return 0;
    }
    if (!memcmp(magic_header, MAGIC_V1, sizeof(magic_t))) {
        *out_version = PERSISTENCE_VERSION_1;
        return 0;
    }
    return -1;
}

avs_error_t anjay_server_object_restore(anjay_t *anjay,
                                        avs_stream_t *in_stream) {
    assert(anjay);

    const anjay_dm_object_def_t *const *server_obj =
            _anjay_dm_find_object_by_oid(anjay, ANJAY_DM_OID_SERVER);
    server_repr_t *repr = _anjay_serv_get(server_obj);
    if (!repr) {
        return avs_errno(AVS_EBADF);
    }
    server_repr_t backup = *repr;
    avs_persistence_context_t restore_ctx =
            avs_persistence_restore_context_create(in_stream);

    magic_t magic_header;
    avs_error_t err = avs_persistence_bytes(&restore_ctx, magic_header,
                                            sizeof(magic_header));
    if (avs_is_err(err)) {
        persistence_log(WARNING, "Could not read Server Object header");
        return err;
    }
    server_persistence_version_t persistence_version;
    if (check_magic_header(magic_header, &persistence_version)) {
        persistence_log(WARNING, "Header magic constant mismatch");
        return avs_errno(AVS_EBADMSG);
    }

    repr->instances = NULL;
    err = avs_persistence_list(&restore_ctx,
                               (AVS_LIST(void) *) &repr->instances,
                               sizeof(server_instance_t),
                               server_instance_persistence_handler,
                               &persistence_version, NULL);
    if (avs_is_ok(err) && _anjay_serv_object_validate(repr)) {
        err = avs_errno(AVS_EBADMSG);
    }
    if (avs_is_err(err)) {
        _anjay_serv_destroy_instances(&repr->instances);
        repr->instances = backup.instances;
    } else {
        _anjay_serv_destroy_instances(&backup.instances);
        _anjay_serv_clear_modified(repr);
        persistence_log(INFO, "Server Object state restored");
    }
    return err;
}

#    ifdef ANJAY_TEST
#        include "test/persistence.c"
#    endif

#else // WITH_AVS_PERSISTENCE

avs_error_t anjay_server_object_persist(anjay_t *anjay,
                                        avs_stream_t *out_stream) {
    (void) anjay;
    (void) out_stream;
    persistence_log(ERROR, "Persistence not compiled in");
    return avs_errno(AVS_ENOTSUP);
}

avs_error_t anjay_server_object_restore(anjay_t *anjay,
                                        avs_stream_t *in_stream) {
    (void) anjay;
    (void) in_stream;
    persistence_log(ERROR, "Persistence not compiled in");
    return avs_errno(AVS_ENOTSUP);
}

#endif // WITH_AVS_PERSISTENCE
