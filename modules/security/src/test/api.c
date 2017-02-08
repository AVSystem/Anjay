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

#include <avsystem/commons/unit/test.h>

#include <anjay_test/utils.h>

typedef struct {
    const anjay_dm_object_def_t **obj;
} security_test_env_t;

#define SCOPED_SERVER_TEST_ENV(Name)                       \
    SCOPED_PTR(security_test_env_t, security_test_env_destroy) \
    Name = security_test_env_create();

static security_test_env_t *
security_test_env_create() {
    security_test_env_t *env = (__typeof__(env)) calloc(1, sizeof(*env));
    AVS_UNIT_ASSERT_NOT_NULL(env);
    env->obj = anjay_security_object_create();
    AVS_UNIT_ASSERT_NOT_NULL(env->obj);
    return env;
}

static void security_test_env_destroy(security_test_env_t **env) {
    anjay_security_object_delete((*env)->obj);
    free(*env);
}

static const anjay_security_instance_t instance1 = {
    .ssid = 0,
    .server_uri = "coap://1.2.3.4",
    .bootstrap_server = false,
    .security_mode = ANJAY_UDP_SECURITY_NOSEC,
    .client_holdoff_s = -1,
    .bootstrap_timeout_s = -1
};

static const anjay_security_instance_t instance2 = {
    .ssid = 1,
    .server_uri = "coap://1.2.3.4",
    .bootstrap_server = false,
    .security_mode = ANJAY_UDP_SECURITY_NOSEC,
    .client_holdoff_s = -1,
    .bootstrap_timeout_s = -1
};

AVS_UNIT_TEST(security_object_api, add_instances_with_duplicated_ids) {
    SCOPED_SERVER_TEST_ENV(env);
    anjay_iid_t iid = 0;
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_add_instance(env->obj, &instance1, &iid));
    AVS_UNIT_ASSERT_FAILED(anjay_security_object_add_instance(env->obj, &instance2, &iid));
}

AVS_UNIT_TEST(security_object_api, add_instances_with_duplicated_ssids) {
    SCOPED_SERVER_TEST_ENV(env);
    anjay_iid_t iid = 1;
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_add_instance(env->obj, &instance1, &iid));
    iid = 2;
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_add_instance(env->obj, &instance2, &iid));
    iid = 3;
    AVS_UNIT_ASSERT_FAILED(anjay_security_object_add_instance(env->obj, &instance1, &iid));
    AVS_UNIT_ASSERT_FAILED(anjay_security_object_add_instance(env->obj, &instance2, &iid));
}
