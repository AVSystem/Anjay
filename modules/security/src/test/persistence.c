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
    sec_repr_t *stored_repr;
    sec_repr_t *restored_repr;
    avs_stream_abstract_t *stream;
} security_persistence_test_env_t;

#define SCOPED_SECURITY_PERSISTENCE_TEST_ENV(Name)    \
    SCOPED_PTR(security_persistence_test_env_t,       \
               security_persistence_test_env_destroy) \
    Name = security_persistence_test_env_create();

static security_persistence_test_env_t *
security_persistence_test_env_create() {
    security_persistence_test_env_t *env =
            (__typeof__(env)) calloc(1, sizeof(*env));
    AVS_UNIT_ASSERT_NOT_NULL(env);
    env->stored = anjay_security_object_create();
    AVS_UNIT_ASSERT_NOT_NULL(env->stored);
    env->restored = anjay_security_object_create();
    AVS_UNIT_ASSERT_NOT_NULL(env->restored);
    env->stream = avs_stream_membuf_create();
    AVS_UNIT_ASSERT_NOT_NULL(env->stream);
    env->stored_repr = _anjay_sec_get(env->stored);
    env->restored_repr = _anjay_sec_get(env->restored);
    return env;
}

static void
security_persistence_test_env_destroy(security_persistence_test_env_t **env) {
    anjay_security_object_delete((*env)->stored);
    anjay_security_object_delete((*env)->restored);
    avs_stream_cleanup(&(*env)->stream);
    free(*env);
}

AVS_UNIT_TEST(security_persistence, empty_store_restore) {
    SCOPED_SECURITY_PERSISTENCE_TEST_ENV(env);
    AVS_UNIT_ASSERT_EQUAL(0, AVS_LIST_SIZE(env->stored_repr->instances));
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_persist(env->stored, env->stream));
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_restore(env->restored, env->stream));
    AVS_UNIT_ASSERT_EQUAL(0, AVS_LIST_SIZE(env->restored_repr->instances));
}

static const char BUFFERS[][50] = {
    "Fitter Happier, more productive                ",
    "comfortable, not drinking too much             ",
    "regular exercise at the gym (3 days a week) ..."
};

static const anjay_security_instance_t BOOTSTRAP_INSTANCE = {
    .ssid = 0,
    .server_uri = "... at ease, eating well",
    .bootstrap_server = true,
    .security_mode = ANJAY_UDP_SECURITY_NOSEC,
    .client_holdoff_s = -1,
    .bootstrap_timeout_s = -1,
    .public_cert_or_psk_identity = (const uint8_t *) BUFFERS[0],
    .public_cert_or_psk_identity_size = sizeof(BUFFERS[0]),
    .private_cert_or_psk_key = (const uint8_t *) BUFFERS[1],
    .private_cert_or_psk_key_size = sizeof(BUFFERS[1]),
    .server_public_key = (const uint8_t *) BUFFERS[2],
    .server_public_key_size = sizeof(BUFFERS[1])
};

static void assert_raw_buffers_equal(const anjay_raw_buffer_t *a,
                                     const anjay_raw_buffer_t *b) {
    AVS_UNIT_ASSERT_EQUAL(a->size, b->size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(a->data, b->data, a->size);
}

static void assert_instances_equal(const sec_instance_t *a,
                                   const sec_instance_t *b) {
    AVS_UNIT_ASSERT_EQUAL(a->iid, b->iid);
    AVS_UNIT_ASSERT_EQUAL_STRING(a->server_uri, b->server_uri);
    AVS_UNIT_ASSERT_EQUAL(a->is_bootstrap, b->is_bootstrap);
    AVS_UNIT_ASSERT_EQUAL((uint32_t) a->security_mode, (uint32_t) b->security_mode);
    assert_raw_buffers_equal(&a->public_cert_or_psk_identity,
                             &b->public_cert_or_psk_identity);
    assert_raw_buffers_equal(&a->private_cert_or_psk_key,
                             &b->private_cert_or_psk_key);
    assert_raw_buffers_equal(&a->server_public_key,
                             &b->server_public_key);
    AVS_UNIT_ASSERT_EQUAL(a->ssid, b->ssid);
    AVS_UNIT_ASSERT_EQUAL(a->holdoff_s, b->holdoff_s);
    AVS_UNIT_ASSERT_EQUAL(a->bs_timeout_s, b->bs_timeout_s);

    AVS_UNIT_ASSERT_EQUAL(a->has_is_bootstrap, b->has_is_bootstrap);
    AVS_UNIT_ASSERT_EQUAL(a->has_security_mode, b->has_security_mode);
    AVS_UNIT_ASSERT_EQUAL(a->has_ssid, b->has_ssid);
}

static void assert_objects_equal(const sec_repr_t *a,
                                 const sec_repr_t *b) {
    AVS_LIST(sec_instance_t) a_it = a->instances;
    AVS_LIST(sec_instance_t) b_it = b->instances;
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(a->instances), AVS_LIST_SIZE(b->instances));
    while (a_it && b_it) {
        assert_instances_equal(a_it, b_it);
        a_it = AVS_LIST_NEXT(a_it);
        b_it = AVS_LIST_NEXT(b_it);
    }
    /* Both should be NULL */
    AVS_UNIT_ASSERT_TRUE(a_it == b_it);
}

AVS_UNIT_TEST(security_persistence, basic_store_restore) {
    SCOPED_SECURITY_PERSISTENCE_TEST_ENV(env);
    anjay_iid_t iid = ANJAY_IID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_security_object_add_instance(env->stored, &BOOTSTRAP_INSTANCE, &iid));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_security_object_persist(env->stored, env->stream));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_security_object_restore(env->restored, env->stream));
    assert_objects_equal(_anjay_sec_get(env->stored),
                         _anjay_sec_get(env->restored));
}

AVS_UNIT_TEST(security_persistence, invalid_object_to_restore) {
    SCOPED_SECURITY_PERSISTENCE_TEST_ENV(env);
    anjay_iid_t iid = ANJAY_IID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_add_instance(
            env->stored, &BOOTSTRAP_INSTANCE, &iid));

    AVS_LIST(sec_instance_t) first_clone =
            _anjay_sec_clone_instances(_anjay_sec_get(env->stored));
    AVS_LIST(sec_instance_t) second_clone =
            _anjay_sec_clone_instances(_anjay_sec_get(env->stored));
    /* Two bootstrap servers on the list, this is pretty bad. */
    first_clone->ssid = 2;
    AVS_LIST_APPEND(&env->stored_repr->instances, first_clone);

    /* This is to check that restored object will be untouched on failure */
    AVS_LIST_APPEND(&env->restored_repr->instances, second_clone);

    AVS_UNIT_ASSERT_SUCCESS(
            anjay_security_object_persist(env->stored, env->stream));
    AVS_UNIT_ASSERT_FAILED(
            anjay_security_object_restore(env->restored, env->stream));

    /* Restored Object remains untouched */
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(env->restored_repr->instances),
                          AVS_LIST_SIZE(second_clone));
    assert_instances_equal(AVS_LIST_NTH(env->restored_repr->instances, 0),
                           AVS_LIST_NTH(second_clone, 0));
}
