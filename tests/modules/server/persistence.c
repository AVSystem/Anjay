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

#include <avsystem/commons/avs_stream.h>
#include <avsystem/commons/avs_stream_membuf.h>
#include <avsystem/commons/avs_unit_test.h>

#include "tests/utils/utils.h"

static const anjay_configuration_t CONFIG = {
    .endpoint_name = "test"
};

typedef struct {
    anjay_t *anjay_stored;
    anjay_t *anjay_restored;
    const anjay_dm_object_def_t *const *stored;
    const anjay_dm_object_def_t *const *restored;
    server_repr_t *stored_repr;
    server_repr_t *restored_repr;
    avs_stream_t *stream;
} server_persistence_test_env_t;

#define SCOPED_SERVER_PERSISTENCE_TEST_ENV(Name)    \
    SCOPED_PTR(server_persistence_test_env_t,       \
               server_persistence_test_env_destroy) \
    Name = server_persistence_test_env_create();

static server_persistence_test_env_t *server_persistence_test_env_create(void) {
    server_persistence_test_env_t *env =
            (__typeof__(env)) avs_calloc(1, sizeof(*env));
    AVS_UNIT_ASSERT_NOT_NULL(env);
    env->anjay_stored = anjay_new(&CONFIG);
    AVS_UNIT_ASSERT_NOT_NULL(env->anjay_stored);
    env->anjay_restored = anjay_new(&CONFIG);
    AVS_UNIT_ASSERT_NOT_NULL(env->anjay_stored);
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_install(env->anjay_stored));
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_install(env->anjay_restored));
    env->stream = avs_stream_membuf_create();
    AVS_UNIT_ASSERT_NOT_NULL(env->stream);
    env->stored = _anjay_dm_find_object_by_oid(env->anjay_stored,
                                               ANJAY_DM_OID_SERVER);
    env->restored = _anjay_dm_find_object_by_oid(env->anjay_restored,
                                                 ANJAY_DM_OID_SERVER);
    env->stored_repr = _anjay_serv_get(env->stored);
    env->restored_repr = _anjay_serv_get(env->restored);
    return env;
}

static void
server_persistence_test_env_destroy(server_persistence_test_env_t **env) {
    anjay_delete((*env)->anjay_stored);
    anjay_delete((*env)->anjay_restored);
    avs_stream_cleanup(&(*env)->stream);
    avs_free(*env);
}

static void assert_instances_equal(const server_instance_t *a,
                                   const server_instance_t *b) {
    AVS_UNIT_ASSERT_EQUAL(a->iid, b->iid);
    AVS_UNIT_ASSERT_EQUAL(a->has_binding, b->has_binding);
    if (a->has_binding) {
        AVS_UNIT_ASSERT_EQUAL_STRING(a->binding, b->binding);
    }
    AVS_UNIT_ASSERT_EQUAL(a->has_ssid, b->has_ssid);
    if (a->has_ssid) {
        AVS_UNIT_ASSERT_EQUAL(a->ssid, b->ssid);
    }
    AVS_UNIT_ASSERT_EQUAL(a->has_lifetime, b->has_lifetime);
    if (a->has_lifetime) {
        AVS_UNIT_ASSERT_EQUAL(a->lifetime, b->lifetime);
    }
    AVS_UNIT_ASSERT_EQUAL(a->has_default_min_period, b->has_default_min_period);
    if (a->has_default_min_period) {
        AVS_UNIT_ASSERT_EQUAL(a->default_min_period, b->default_min_period);
    }
    AVS_UNIT_ASSERT_EQUAL(a->has_default_max_period, b->has_default_max_period);
    if (a->has_default_max_period) {
        AVS_UNIT_ASSERT_EQUAL(a->default_max_period, b->default_max_period);
    }
    AVS_UNIT_ASSERT_EQUAL(a->has_disable_timeout, b->has_disable_timeout);
    if (a->has_disable_timeout) {
        AVS_UNIT_ASSERT_EQUAL(a->disable_timeout, b->disable_timeout);
    }
    AVS_UNIT_ASSERT_EQUAL(a->has_notification_storing,
                          b->has_notification_storing);
    if (a->has_notification_storing) {
        AVS_UNIT_ASSERT_EQUAL(a->notification_storing, b->notification_storing);
    }
}

AVS_UNIT_TEST(server_persistence, empty_store_restore) {
    SCOPED_SERVER_PERSISTENCE_TEST_ENV(env);
    AVS_UNIT_ASSERT_EQUAL(0, AVS_LIST_SIZE(env->stored_repr->instances));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_server_object_persist(env->anjay_stored, env->stream));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_server_object_restore(env->anjay_restored, env->stream));
    AVS_UNIT_ASSERT_EQUAL(0, AVS_LIST_SIZE(env->restored_repr->instances));
}

AVS_UNIT_TEST(server_persistence, nonempty_store_restore_version_1) {
    SCOPED_SERVER_PERSISTENCE_TEST_ENV(env);
    /*
     * This represents following server instance persisted with version=1:
     * const anjay_server_instance_t instance = {
     *     .ssid = 42,
     *     .lifetime = 9001,
     *     .default_min_period = -1,
     *     .default_max_period = -1,
     *     .disable_timeout = -1,
     *     .binding = "UQ",
     *     .notification_storing = true
     * };
     */
    const char persisted_binary[] =
            "\x53\x52\x56\x01\x00\x00\x00\x01\x00\x01\x01\x01\x01\x01\x00\x2a"
            "\x00\x00\x23\x29\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\x01\x55\x51\x00\x00\x00\x00\x00\x00";
    const size_t persisted_binary_size = sizeof(persisted_binary) - 1;
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_write(
            env->stream, persisted_binary, persisted_binary_size));

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_server_object_restore(env->anjay_restored, env->stream));
    AVS_UNIT_ASSERT_EQUAL(1, AVS_LIST_SIZE(env->restored_repr->instances));
    const server_instance_t expected_server_instance = {
        .iid = 1,
        .ssid = 42,
        .lifetime = 9001,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .has_binding = true,
        .binding = "UQ",
        .notification_storing = true,
        .has_ssid = true,
        .has_lifetime = true,
        .has_notification_storing = true,
    };
    assert_instances_equal(&expected_server_instance,
                           env->restored_repr->instances);
}

AVS_UNIT_TEST(server_persistence, nonempty_store_restore) {
    SCOPED_SERVER_PERSISTENCE_TEST_ENV(env);
    const anjay_server_instance_t instance = {
        .ssid = 42,
        .lifetime = 9001,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "UQ",
        .notification_storing = true,
    };

    anjay_iid_t iid = 1;
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_add_instance(
            env->anjay_stored, &instance, &iid));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_server_object_persist(env->anjay_stored, env->stream));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_server_object_restore(env->anjay_restored, env->stream));
    AVS_UNIT_ASSERT_EQUAL(1, AVS_LIST_SIZE(env->restored_repr->instances));
    const server_instance_t expected_server_instance = {
        .iid = 1,
        .has_binding = true,
        .binding = "UQ",
        .has_ssid = true,
        .ssid = 42,
        .has_lifetime = true,
        .lifetime = 9001,
        .has_notification_storing = true,
        .notification_storing = true,
    };
    assert_instances_equal(&expected_server_instance,
                           env->restored_repr->instances);
}

AVS_UNIT_TEST(server_persistence, modification_flag_add_instance) {
    SCOPED_SERVER_PERSISTENCE_TEST_ENV(env);
    /* At the beginning server object is not modified */
    AVS_UNIT_ASSERT_FALSE(anjay_server_object_is_modified(env->anjay_stored));
    /* Invalid instance does not change the modification flag */
    anjay_iid_t iid = ANJAY_ID_INVALID;
    const anjay_server_instance_t invalid_instance = {
        .ssid = 0
    };
    AVS_UNIT_ASSERT_FAILED(anjay_server_object_add_instance(
            env->anjay_stored, &invalid_instance, &iid));
    AVS_UNIT_ASSERT_FALSE(anjay_server_object_is_modified(env->anjay_stored));
    /* Same thing applies if the flag already was set to true */
    _anjay_serv_mark_modified(_anjay_serv_get(env->stored));
    AVS_UNIT_ASSERT_FAILED(anjay_server_object_add_instance(
            env->anjay_stored, &invalid_instance, &iid));
    AVS_UNIT_ASSERT_TRUE(anjay_server_object_is_modified(env->anjay_stored));
    _anjay_serv_clear_modified(_anjay_serv_get(env->stored));

    const anjay_server_instance_t instance = {
        .ssid = 42,
        .lifetime = 9001,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U",
        .notification_storing = true
    };
    /* And valid instance does change the flag */
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_add_instance(
            env->anjay_stored, &instance, &iid));
    AVS_UNIT_ASSERT_TRUE(anjay_server_object_is_modified(env->anjay_stored));
}

AVS_UNIT_TEST(server_persistence, modification_flag_purge) {
    SCOPED_SERVER_PERSISTENCE_TEST_ENV(env);
    /* Purged object remains unmodified after purge */
    anjay_server_object_purge(env->anjay_stored);
    AVS_UNIT_ASSERT_FALSE(anjay_server_object_is_modified(env->anjay_stored));

    anjay_iid_t iid = ANJAY_ID_INVALID;
    const anjay_server_instance_t instance = {
        .ssid = 42,
        .lifetime = 9001,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U",
        .notification_storing = true
    };
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_add_instance(
            env->anjay_stored, &instance, &iid));
    AVS_UNIT_ASSERT_TRUE(anjay_server_object_is_modified(env->anjay_stored));
    /* Simulate persistence operation. */
    _anjay_serv_clear_modified(_anjay_serv_get(env->stored));
    AVS_UNIT_ASSERT_FALSE(anjay_server_object_is_modified(env->anjay_stored));
    anjay_server_object_purge(env->anjay_stored);
    AVS_UNIT_ASSERT_TRUE(anjay_server_object_is_modified(env->anjay_stored));
}
