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
    ANJAY_MUTEX_LOCK(anjay_unlocked, env->anjay_stored);
    env->stored =
            *_anjay_dm_find_object_by_oid(anjay_unlocked, ANJAY_DM_OID_SERVER);
    ANJAY_MUTEX_UNLOCK(env->anjay_stored);
    ANJAY_MUTEX_LOCK(anjay_unlocked, env->anjay_restored);
    env->restored =
            *_anjay_dm_find_object_by_oid(anjay_unlocked, ANJAY_DM_OID_SERVER);
    ANJAY_MUTEX_UNLOCK(env->anjay_restored);
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
    AVS_UNIT_ASSERT_EQUAL_STRING(a->binding.data, b->binding.data);
    AVS_UNIT_ASSERT_EQUAL(a->ssid, b->ssid);
    AVS_UNIT_ASSERT_EQUAL(a->lifetime, b->lifetime);
    AVS_UNIT_ASSERT_EQUAL(a->default_min_period, b->default_min_period);
    AVS_UNIT_ASSERT_EQUAL(a->default_max_period, b->default_max_period);
    AVS_UNIT_ASSERT_EQUAL(a->disable_timeout, b->disable_timeout);
    AVS_UNIT_ASSERT_EQUAL(a->notification_storing, b->notification_storing);
#ifdef ANJAY_WITH_LWM2M11
    AVS_UNIT_ASSERT_EQUAL(a->last_alert, b->last_alert);
    AVS_UNIT_ASSERT_EQUAL(a->last_bootstrapped_timestamp,
                          b->last_bootstrapped_timestamp);
    AVS_UNIT_ASSERT_EQUAL(a->bootstrap_on_registration_failure,
                          b->bootstrap_on_registration_failure);
    AVS_UNIT_ASSERT_EQUAL(a->server_communication_retry_count,
                          b->server_communication_retry_count);
    AVS_UNIT_ASSERT_EQUAL(a->server_communication_retry_timer,
                          b->server_communication_retry_timer);
    AVS_UNIT_ASSERT_EQUAL(a->server_communication_sequence_retry_count,
                          b->server_communication_sequence_retry_count);
    AVS_UNIT_ASSERT_EQUAL(a->server_communication_sequence_delay_timer,
                          b->server_communication_sequence_delay_timer);
    AVS_UNIT_ASSERT_EQUAL(a->preferred_transport, b->preferred_transport);
#    ifdef ANJAY_WITH_SEND
    AVS_UNIT_ASSERT_EQUAL(a->mute_send, b->mute_send);
#    endif // ANJAY_WITH_SEND

    for (size_t i = 0; i < AVS_ARRAY_SIZE(a->present_resources); ++i) {
        AVS_UNIT_ASSERT_EQUAL(a->present_resources[i], b->present_resources[i]);
    }
#endif // ANJAY_WITH_LWM2M11
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
        .binding = {
            .data = "UQ",
        },
        .notification_storing = true,
#ifdef ANJAY_WITH_LWM2M11
        .bootstrap_on_registration_failure = true,
#endif // ANJAY_WITH_LWM2M11
        .present_resources = {
            [SERV_RES_SSID] = true,
            [SERV_RES_LIFETIME] = true,
            [SERV_RES_DISABLE] = true,
            [SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE] = true,
            [SERV_RES_BINDING] = true,
            [SERV_RES_REGISTRATION_UPDATE_TRIGGER] = true,
#ifdef ANJAY_WITH_LWM2M11
            [SERV_RES_BOOTSTRAP_REQUEST_TRIGGER] = true,
            [SERV_RES_BOOTSTRAP_ON_REGISTRATION_FAILURE] = true,
#    ifdef ANJAY_WITH_SEND
            [SERV_RES_MUTE_SEND] = true
#    endif // ANJAY_WITH_SEND
#endif     // ANJAY_WITH_LWM2M11
        }
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
#ifdef ANJAY_WITH_LWM2M11
        .bootstrap_on_registration_failure = &(bool) { false },
        .preferred_transport = 'U',
        .mute_send = true,
        .communication_sequence_retry_count = &(uint32_t) { 2 },
        .communication_sequence_delay_timer = &(uint32_t) { 10 },
#endif // ANJAY_WITH_LWM2M11
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
        .binding = {
            .data = "UQ",
        },
        .ssid = 42,
        .lifetime = 9001,
        .notification_storing = true,
#ifdef ANJAY_WITH_LWM2M11
        .bootstrap_on_registration_failure = false,
        .preferred_transport = 'U',
#    ifdef ANJAY_WITH_SEND
        .mute_send = true,
#    endif // ANJAY_WITH_SEND
        .server_communication_sequence_retry_count = 2,
        .server_communication_sequence_delay_timer = 10,
#endif // ANJAY_WITH_LWM2M11
        .present_resources = {
            [SERV_RES_SSID] = true,
            [SERV_RES_LIFETIME] = true,
            [SERV_RES_DISABLE] = true,
            [SERV_RES_NOTIFICATION_STORING_WHEN_DISABLED_OR_OFFLINE] = true,
            [SERV_RES_BINDING] = true,
            [SERV_RES_REGISTRATION_UPDATE_TRIGGER] = true,
#ifdef ANJAY_WITH_LWM2M11
            [SERV_RES_BOOTSTRAP_REQUEST_TRIGGER] = true,
            [SERV_RES_BOOTSTRAP_ON_REGISTRATION_FAILURE] = true,
            [SERV_RES_SERVER_COMMUNICATION_SEQUENCE_RETRY_COUNT] = true,
            [SERV_RES_SERVER_COMMUNICATION_SEQUENCE_DELAY_TIMER] = true,
            [SERV_RES_PREFERRED_TRANSPORT] = true,
#    ifdef ANJAY_WITH_SEND
            [SERV_RES_MUTE_SEND] = true
#    endif // ANJAY_WITH_SEND
#endif     // ANJAY_WITH_LWM2M11
        }
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
