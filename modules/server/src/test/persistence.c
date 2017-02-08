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

#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream/stream_membuf.h>
#include <avsystem/commons/unit/test.h>

#include <anjay_test/utils.h>

typedef struct {
    const anjay_dm_object_def_t **stored;
    const anjay_dm_object_def_t **restored;
    server_repr_t *stored_repr;
    server_repr_t *restored_repr;
    avs_stream_abstract_t *stream;
} server_persistence_test_env_t;

#define SCOPED_SERVER_PERSISTENCE_TEST_ENV(Name)    \
    SCOPED_PTR(server_persistence_test_env_t,       \
               server_persistence_test_env_destroy) \
    Name = server_persistence_test_env_create();

static server_persistence_test_env_t *
server_persistence_test_env_create() {
    server_persistence_test_env_t *env =
            (__typeof__(env)) calloc(1, sizeof(*env));
    AVS_UNIT_ASSERT_NOT_NULL(env);
    env->stored = anjay_server_object_create();
    AVS_UNIT_ASSERT_NOT_NULL(env->stored);
    env->restored = anjay_server_object_create();
    AVS_UNIT_ASSERT_NOT_NULL(env->restored);
    env->stream = avs_stream_membuf_create();
    AVS_UNIT_ASSERT_NOT_NULL(env->stream);
    env->stored_repr = _anjay_serv_get(env->stored);
    env->restored_repr = _anjay_serv_get(env->restored);
    return env;
}

static void
server_persistence_test_env_destroy(server_persistence_test_env_t **env) {
    anjay_server_object_delete((*env)->stored);
    anjay_server_object_delete((*env)->restored);
    avs_stream_cleanup(&(*env)->stream);
    free(*env);
}

static void assert_instances_equal(const server_instance_t *a,
                                   const server_instance_t *b) {
    AVS_UNIT_ASSERT_EQUAL(a->iid, b->iid);
    AVS_UNIT_ASSERT_EQUAL(a->has_ssid, b->has_ssid);
    if (a->has_ssid) {
        AVS_UNIT_ASSERT_EQUAL(a->data.ssid, b->data.ssid);
    }
    AVS_UNIT_ASSERT_EQUAL(a->has_binding, b->has_binding);
    if (a->has_binding) {
        AVS_UNIT_ASSERT_EQUAL(a->data.binding, b->data.binding);
    }
    AVS_UNIT_ASSERT_EQUAL(a->has_lifetime, b->has_lifetime);
    if (a->has_lifetime) {
        AVS_UNIT_ASSERT_EQUAL(a->data.lifetime, b->data.lifetime);
    }
    AVS_UNIT_ASSERT_EQUAL(a->has_notification_storing, b->has_notification_storing);
    if (a->has_notification_storing) {
        AVS_UNIT_ASSERT_EQUAL(a->data.notification_storing, b->data.notification_storing);
    }
    AVS_UNIT_ASSERT_EQUAL(a->data.default_min_period, b->data.default_min_period);
    AVS_UNIT_ASSERT_EQUAL(a->data.default_max_period, b->data.default_max_period);
    AVS_UNIT_ASSERT_EQUAL(a->data.disable_timeout, b->data.disable_timeout);
}

AVS_UNIT_TEST(server_persistence, empty_store_restore) {
    SCOPED_SERVER_PERSISTENCE_TEST_ENV(env);
    AVS_UNIT_ASSERT_EQUAL(0, AVS_LIST_SIZE(env->stored_repr->instances));
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_persist(env->stored, env->stream));
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_restore(env->restored, env->stream));
    AVS_UNIT_ASSERT_EQUAL(0, AVS_LIST_SIZE(env->restored_repr->instances));
}

AVS_UNIT_TEST(server_persistence, nonempty_store_restore) {
    SCOPED_SERVER_PERSISTENCE_TEST_ENV(env);
    const anjay_server_instance_t instance = {
        .ssid = 42,
        .lifetime = 9001,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = ANJAY_BINDING_U,
        .notification_storing = true
    };
    anjay_iid_t iid = 1;
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_add_instance(env->stored, &instance, &iid));
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_persist(env->stored, env->stream));
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_restore(env->restored, env->stream));
    AVS_UNIT_ASSERT_EQUAL(1, AVS_LIST_SIZE(env->restored_repr->instances));
    const server_instance_t expected_server_instance = {
        .iid = 1,
        .data = instance,
        .has_ssid = true,
        .has_binding = true,
        .has_lifetime = true,
        .has_notification_storing = true
    };
    assert_instances_equal(&expected_server_instance, env->restored_repr->instances);
}
