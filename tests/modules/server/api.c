/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_unit_test.h>

#include "tests/utils/utils.h"

static const anjay_configuration_t CONFIG = {
    .endpoint_name = "test"
};

typedef struct {
    anjay_t *anjay;
} server_test_env_t;

#define SCOPED_SERVER_TEST_ENV(Name)                       \
    SCOPED_PTR(server_test_env_t, server_test_env_destroy) \
    Name = server_test_env_create();

static server_test_env_t *server_test_env_create(void) {
    server_test_env_t *env = (__typeof__(env)) avs_calloc(1, sizeof(*env));
    AVS_UNIT_ASSERT_NOT_NULL(env);
    env->anjay = anjay_new(&CONFIG);
    AVS_UNIT_ASSERT_NOT_NULL(env->anjay);
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_install(env->anjay));
    return env;
}

static void server_test_env_destroy(server_test_env_t **env) {
    anjay_delete((*env)->anjay);
    avs_free(*env);
}

static const anjay_server_instance_t instance1 = {
    .ssid = 1,
    .lifetime = 42,
    .default_min_period = -1,
    .default_max_period = -1,
    .disable_timeout = -1,
    .binding = "U",
    .notification_storing = false
};

static const anjay_server_instance_t instance2 = {
    .ssid = 2,
    .lifetime = 424,
    .default_min_period = -1,
    .default_max_period = -1,
    .disable_timeout = -1,
    .binding = "U",
    .notification_storing = false
};

AVS_UNIT_TEST(server_object_api, add_instances_with_duplicated_ids) {
    SCOPED_SERVER_TEST_ENV(env);
    anjay_iid_t iid = 1;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_server_object_add_instance(env->anjay, &instance1, &iid));
    AVS_UNIT_ASSERT_FAILED(
            anjay_server_object_add_instance(env->anjay, &instance2, &iid));
}

AVS_UNIT_TEST(server_object_api, add_instances_with_duplicated_ssids) {
    SCOPED_SERVER_TEST_ENV(env);
    anjay_iid_t iid = 1;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_server_object_add_instance(env->anjay, &instance1, &iid));
    iid = 2;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_server_object_add_instance(env->anjay, &instance2, &iid));
    iid = 3;
    AVS_UNIT_ASSERT_FAILED(
            anjay_server_object_add_instance(env->anjay, &instance1, &iid));
    AVS_UNIT_ASSERT_FAILED(
            anjay_server_object_add_instance(env->anjay, &instance2, &iid));
}

AVS_UNIT_TEST(server_object_api, set_lifetime) {
    SCOPED_SERVER_TEST_ENV(env);
    anjay_iid_t iid = 1;
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_server_object_add_instance(env->anjay, &instance1, &iid));
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_server_object_set_lifetime(env->anjay, iid, 1234));
}
