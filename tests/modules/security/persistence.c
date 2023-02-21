/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
    anjay_dm_installed_object_t stored;
    anjay_dm_installed_object_t restored;
    sec_repr_t *stored_repr;
    sec_repr_t *restored_repr;
    avs_stream_t *stream;
} security_persistence_test_env_t;

#define SCOPED_SECURITY_PERSISTENCE_TEST_ENV(Name)    \
    SCOPED_PTR(security_persistence_test_env_t,       \
               security_persistence_test_env_destroy) \
    Name = security_persistence_test_env_create();

static security_persistence_test_env_t *
security_persistence_test_env_create(void) {
    security_persistence_test_env_t *env =
            (__typeof__(env)) avs_calloc(1, sizeof(*env));
    AVS_UNIT_ASSERT_NOT_NULL(env);
    env->anjay_stored = anjay_new(&CONFIG);
    AVS_UNIT_ASSERT_NOT_NULL(env->anjay_stored);
    env->anjay_restored = anjay_new(&CONFIG);
    AVS_UNIT_ASSERT_NOT_NULL(env->anjay_restored);
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_install(env->anjay_stored));
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_install(env->anjay_restored));
    env->stream = avs_stream_membuf_create();
    AVS_UNIT_ASSERT_NOT_NULL(env->stream);
    ANJAY_MUTEX_LOCK(anjay_unlocked, env->anjay_stored);
    env->stored = *_anjay_dm_find_object_by_oid(anjay_unlocked,
                                                ANJAY_DM_OID_SECURITY);
    ANJAY_MUTEX_UNLOCK(env->anjay_stored);
    ANJAY_MUTEX_LOCK(anjay_unlocked, env->anjay_restored);
    env->restored = *_anjay_dm_find_object_by_oid(anjay_unlocked,
                                                  ANJAY_DM_OID_SECURITY);
    ANJAY_MUTEX_UNLOCK(env->anjay_restored);
    env->stored_repr = _anjay_sec_get(env->stored);
    env->restored_repr = _anjay_sec_get(env->restored);
    return env;
}

static void
security_persistence_test_env_destroy(security_persistence_test_env_t **env) {
    anjay_delete((*env)->anjay_stored);
    anjay_delete((*env)->anjay_restored);
    avs_stream_cleanup(&(*env)->stream);
    avs_free(*env);
}

AVS_UNIT_TEST(security_persistence, empty_store_restore) {
    SCOPED_SECURITY_PERSISTENCE_TEST_ENV(env);
    AVS_UNIT_ASSERT_EQUAL(0, AVS_LIST_SIZE(env->stored_repr->instances));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_security_object_persist(env->anjay_stored, env->stream));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_security_object_restore(env->anjay_restored, env->stream));
    AVS_UNIT_ASSERT_EQUAL(0, AVS_LIST_SIZE(env->restored_repr->instances));
}

static const char BUFFERS[][50] = {
    "Fitter Happier, more productive                ",
    "comfortable, not drinking too much             ",
    "regular exercise at the gym (3 days a week) ..."
};

static const anjay_security_instance_t BOOTSTRAP_INSTANCE = {
    .ssid = 0,
    .server_uri = "coap://at.ease/eating?well",
    .bootstrap_server = true,
    .security_mode = ANJAY_SECURITY_NOSEC,
    .client_holdoff_s = -1,
    .bootstrap_timeout_s = -1,
    .public_cert_or_psk_identity = (const uint8_t *) BUFFERS[0],
    .public_cert_or_psk_identity_size = sizeof(BUFFERS[0]),
    .private_cert_or_psk_key = (const uint8_t *) BUFFERS[1],
    .private_cert_or_psk_key_size = sizeof(BUFFERS[1]),
    .server_public_key = (const uint8_t *) BUFFERS[2],
    .server_public_key_size = sizeof(BUFFERS[2])
};

static void assert_raw_buffers_equal(const anjay_raw_buffer_t *a,
                                     const anjay_raw_buffer_t *b) {
    AVS_UNIT_ASSERT_EQUAL(a->size, b->size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(a->data, b->data, a->size);
}

#ifdef ANJAY_WITH_SECURITY_STRUCTURED
static const avs_crypto_security_info_union_t *
get_actual_security_info(const avs_crypto_security_info_union_t *info) {
    switch (info->source) {
    case AVS_CRYPTO_DATA_SOURCE_ARRAY:
        AVS_UNIT_ASSERT_EQUAL(info->info.array.element_count, 1);
        AVS_UNIT_ASSERT_EQUAL(info->info.array.array_ptr[0].type, info->type);
        return get_actual_security_info(&info->info.array.array_ptr[0]);
    case AVS_CRYPTO_DATA_SOURCE_LIST:
        AVS_UNIT_ASSERT_NOT_NULL(info->info.list.list_head);
        AVS_UNIT_ASSERT_NULL(AVS_LIST_NEXT(info->info.list.list_head));
        AVS_UNIT_ASSERT_EQUAL(info->info.list.list_head->type, info->type);
        return get_actual_security_info(info->info.list.list_head);
    default:
        return info;
    }
}
#endif // ANJAY_WITH_SECURITY_STRUCTURED

static void assert_sec_key_or_data_equal(const sec_key_or_data_t *a,
                                         const sec_key_or_data_t *b) {
    AVS_UNIT_ASSERT_EQUAL(a->type, b->type);
    switch (a->type) {
    case SEC_KEY_AS_DATA:
        assert_raw_buffers_equal(&a->value.data, &b->value.data);
        break;
    case SEC_KEY_AS_KEY_EXTERNAL:
    case SEC_KEY_AS_KEY_OWNED: {
#ifdef ANJAY_WITH_SECURITY_STRUCTURED
        const avs_crypto_security_info_union_t *info_a =
                get_actual_security_info(&a->value.key.info);
        const avs_crypto_security_info_union_t *info_b =
                get_actual_security_info(&b->value.key.info);
        AVS_UNIT_ASSERT_EQUAL(info_a->type, info_b->type);
        AVS_UNIT_ASSERT_EQUAL(info_a->source, AVS_CRYPTO_DATA_SOURCE_BUFFER);
        AVS_UNIT_ASSERT_EQUAL(info_b->source, AVS_CRYPTO_DATA_SOURCE_BUFFER);
        AVS_UNIT_ASSERT_NULL(info_a->info.buffer.password);
        AVS_UNIT_ASSERT_NULL(info_b->info.buffer.password);
        AVS_UNIT_ASSERT_EQUAL(info_a->info.buffer.buffer_size,
                              info_b->info.buffer.buffer_size);
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(info_a->info.buffer.buffer,
                                          info_b->info.buffer.buffer,
                                          info_a->info.buffer.buffer_size);
#else  // ANJAY_WITH_SECURITY_STRUCTURED
        AVS_UNIT_ASSERT_NULL("Unsupported sec_key_or_data_t type");
#endif // ANJAY_WITH_SECURITY_STRUCTURED
        break;
    }
    }
}

static void assert_instances_equal(const sec_instance_t *a,
                                   const sec_instance_t *b) {
    AVS_UNIT_ASSERT_EQUAL(a->iid, b->iid);
    AVS_UNIT_ASSERT_EQUAL_STRING(a->server_uri, b->server_uri);
    AVS_UNIT_ASSERT_EQUAL(a->is_bootstrap, b->is_bootstrap);
    AVS_UNIT_ASSERT_EQUAL((uint32_t) a->security_mode,
                          (uint32_t) b->security_mode);
    assert_sec_key_or_data_equal(&a->public_cert_or_psk_identity,
                                 &b->public_cert_or_psk_identity);
    assert_sec_key_or_data_equal(&a->private_cert_or_psk_key,
                                 &b->private_cert_or_psk_key);
    assert_raw_buffers_equal(&a->server_public_key, &b->server_public_key);
    AVS_UNIT_ASSERT_EQUAL(a->ssid, b->ssid);
    AVS_UNIT_ASSERT_EQUAL(a->holdoff_s, b->holdoff_s);
    AVS_UNIT_ASSERT_EQUAL(a->bs_timeout_s, b->bs_timeout_s);

    for (size_t i = 0; i < AVS_ARRAY_SIZE(a->present_resources); ++i) {
        AVS_UNIT_ASSERT_EQUAL(a->present_resources[i], b->present_resources[i]);
    }
}

static void assert_objects_equal(const sec_repr_t *a, const sec_repr_t *b) {
    AVS_LIST(sec_instance_t) a_it = a->instances;
    AVS_LIST(sec_instance_t) b_it = b->instances;
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(a->instances),
                          AVS_LIST_SIZE(b->instances));
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
    anjay_iid_t iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_add_instance(
            env->anjay_stored, &BOOTSTRAP_INSTANCE, &iid));
    AVS_UNIT_ASSERT_TRUE(anjay_security_object_is_modified(env->anjay_stored));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_security_object_persist(env->anjay_stored, env->stream));
    AVS_UNIT_ASSERT_FALSE(anjay_security_object_is_modified(env->anjay_stored));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_security_object_restore(env->anjay_restored, env->stream));
    assert_objects_equal(_anjay_sec_get(env->stored),
                         _anjay_sec_get(env->restored));
}

#ifdef ANJAY_WITH_SECURITY_STRUCTURED
AVS_UNIT_TEST(security_persistence, structured_store_restore) {
    const anjay_security_instance_t BOOTSTRAP_INSTANCE_STRUCTURED = {
        .ssid = 0,
        .server_uri = "coap://at.ease/eating?well",
        .bootstrap_server = true,
        .security_mode = ANJAY_SECURITY_NOSEC,
        .client_holdoff_s = -1,
        .bootstrap_timeout_s = -1,
        .public_cert = avs_crypto_certificate_chain_info_from_buffer(
                BUFFERS[0], sizeof(BUFFERS[0])),
        .private_key = avs_crypto_private_key_info_from_buffer(
                BUFFERS[1], sizeof(BUFFERS[1]), NULL),
        .server_public_key = (const uint8_t *) BUFFERS[2],
        .server_public_key_size = sizeof(BUFFERS[2])
    };

    SCOPED_SECURITY_PERSISTENCE_TEST_ENV(env);
    anjay_iid_t iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_add_instance(
            env->anjay_stored, &BOOTSTRAP_INSTANCE_STRUCTURED, &iid));
    AVS_UNIT_ASSERT_TRUE(anjay_security_object_is_modified(env->anjay_stored));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_security_object_persist(env->anjay_stored, env->stream));
    AVS_UNIT_ASSERT_FALSE(anjay_security_object_is_modified(env->anjay_stored));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_security_object_restore(env->anjay_restored, env->stream));
    assert_objects_equal(_anjay_sec_get(env->stored),
                         _anjay_sec_get(env->restored));
}
#endif // ANJAY_WITH_SECURITY_STRUCTURED

AVS_UNIT_TEST(security_persistence, invalid_object_to_restore) {
    SCOPED_SECURITY_PERSISTENCE_TEST_ENV(env);
    anjay_iid_t iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_add_instance(
            env->anjay_stored, &BOOTSTRAP_INSTANCE, &iid));

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
            anjay_security_object_persist(env->anjay_stored, env->stream));

    AVS_UNIT_ASSERT_FALSE(
            anjay_security_object_is_modified(env->anjay_restored));
    AVS_UNIT_ASSERT_FAILED(
            anjay_security_object_restore(env->anjay_restored, env->stream));
    AVS_UNIT_ASSERT_FALSE(
            anjay_security_object_is_modified(env->anjay_restored));

    /* Restored Object remains untouched */
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(env->restored_repr->instances),
                          AVS_LIST_SIZE(second_clone));
    assert_instances_equal(AVS_LIST_NTH(env->restored_repr->instances, 0),
                           AVS_LIST_NTH(second_clone, 0));
}

AVS_UNIT_TEST(security_persistence, modification_flag_add_instance) {
    SCOPED_SECURITY_PERSISTENCE_TEST_ENV(env);
    /* At the beginning security object is not modified */
    AVS_UNIT_ASSERT_FALSE(anjay_security_object_is_modified(env->anjay_stored));
    /* Invalid instance does not change the modification flag */
    anjay_iid_t iid = ANJAY_ID_INVALID;
    const anjay_security_instance_t invalid_instance = {
        .server_uri = ""
    };
    AVS_UNIT_ASSERT_FAILED(anjay_security_object_add_instance(
            env->anjay_stored, &invalid_instance, &iid));
    AVS_UNIT_ASSERT_FALSE(anjay_security_object_is_modified(env->anjay_stored));
    /* Same thing applies if the flag already was set to true */
    _anjay_sec_mark_modified(_anjay_sec_get(env->stored));
    AVS_UNIT_ASSERT_FAILED(anjay_security_object_add_instance(
            env->anjay_stored, &invalid_instance, &iid));
    AVS_UNIT_ASSERT_TRUE(anjay_security_object_is_modified(env->anjay_stored));
    _anjay_sec_clear_modified(_anjay_sec_get(env->stored));

    /* And valid instance does change the flag */
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_add_instance(
            env->anjay_stored, &BOOTSTRAP_INSTANCE, &iid));
    AVS_UNIT_ASSERT_TRUE(anjay_security_object_is_modified(env->anjay_stored));
}

AVS_UNIT_TEST(security_persistence, modification_flag_purge) {
    SCOPED_SECURITY_PERSISTENCE_TEST_ENV(env);
    /* Purged object remains unmodified after purge */
    anjay_security_object_purge(env->anjay_stored);
    AVS_UNIT_ASSERT_FALSE(anjay_security_object_is_modified(env->anjay_stored));

    anjay_iid_t iid = ANJAY_ID_INVALID;
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_add_instance(
            env->anjay_stored, &BOOTSTRAP_INSTANCE, &iid));
    AVS_UNIT_ASSERT_TRUE(anjay_security_object_is_modified(env->anjay_stored));
    /* Simulate persistence operation. */
    _anjay_sec_clear_modified(_anjay_sec_get(env->stored));
    AVS_UNIT_ASSERT_FALSE(anjay_security_object_is_modified(env->anjay_stored));
    anjay_security_object_purge(env->anjay_stored);
    AVS_UNIT_ASSERT_TRUE(anjay_security_object_is_modified(env->anjay_stored));
}
