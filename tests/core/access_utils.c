/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <avsystem/commons/avs_stream_membuf.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_test.h>

#include <stdarg.h>
#include <stdio.h>

#include <anjay/access_control.h>
#include <anjay/core.h>
#include <anjay/security.h>
#include <anjay/server.h>

#include "src/anjay_modules/anjay_notify.h"
#include "src/core/anjay_access_utils_private.h"
#include "src/core/anjay_core.h"
#include "src/core/servers/anjay_servers_internal.h"

#include "tests/utils/utils.h"

#ifdef ANJAY_WITH_ACCESS_CONTROL

static int dummy_list_instances(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj,
                                anjay_dm_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj;
    // We emit instances 1, 2, 3 so validation passes
    anjay_dm_emit(ctx, 1);
    anjay_dm_emit(ctx, 2);
    anjay_dm_emit(ctx, 3);
    return 0;
}

static const anjay_dm_object_def_t OBJ_1234 = {
    .oid = 1234,
    .handlers = {
        .list_instances = dummy_list_instances
    }
};
static const anjay_dm_object_def_t *OBJ_DEF_1234 = &OBJ_1234;

static const anjay_dm_object_def_t OBJ_1235 = {
    .oid = 1235,
    .handlers = {
        .list_instances = dummy_list_instances
    }
};
static const anjay_dm_object_def_t *OBJ_DEF_1235 = &OBJ_1235;

static const anjay_dm_object_def_t OBJ_1236 = {
    .oid = 1236,
    .handlers = {
        .list_instances = dummy_list_instances
    }
};
static const anjay_dm_object_def_t *OBJ_DEF_1236 = &OBJ_1236;

typedef struct {
    anjay_t *anjay;
    anjay_iid_t sec_iid1;
    anjay_iid_t sec_iid2;
    anjay_iid_t serv_iid1;
    anjay_iid_t serv_iid2;
} core_access_test_env_t;

static const anjay_configuration_t CONFIG = {
    .endpoint_name = "test"
};

static void add_server_and_security_1(core_access_test_env_t *env) {
    static const anjay_security_instance_t sec_instance_1 = {
        .ssid = 1,
        .server_uri = "coap://1.2.3.4",
        .bootstrap_server = false,
        .security_mode = ANJAY_SECURITY_NOSEC,
        .client_holdoff_s = -1,
        .bootstrap_timeout_s = -1
    };
    env->sec_iid1 = 1;

    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_add_instance(
            env->anjay, &sec_instance_1, &env->sec_iid1));

    static const anjay_server_instance_t serv_instance_1 = {
        .ssid = 1,
        .lifetime = 42,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U",
        .notification_storing = false
    };
    env->serv_iid1 = 1;
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_add_instance(
            env->anjay, &serv_instance_1, &env->serv_iid1));
}

static void add_server_and_security_2(core_access_test_env_t *env) {
    static const anjay_security_instance_t sec_instance_2 = {
        .ssid = 2,
        .server_uri = "coap://4.3.2.1",
        .bootstrap_server = false,
        .security_mode = ANJAY_SECURITY_NOSEC,
        .client_holdoff_s = -1,
        .bootstrap_timeout_s = -1
    };
    env->sec_iid2 = 2;
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_add_instance(
            env->anjay, &sec_instance_2, &env->sec_iid2));

    static const anjay_server_instance_t serv_instance_2 = {
        .ssid = 2,
        .lifetime = 42,
        .default_min_period = -1,
        .default_max_period = -1,
        .disable_timeout = -1,
        .binding = "U",
        .notification_storing = false
    };
    env->serv_iid2 = 2;
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_add_instance(
            env->anjay, &serv_instance_2, &env->serv_iid2));
}

static core_access_test_env_t *core_access_test_env_create(void) {
    core_access_test_env_t *env = (__typeof__(env)) avs_calloc(1, sizeof(*env));
    AVS_UNIT_ASSERT_NOT_NULL(env);
    env->anjay = anjay_new(&CONFIG);
    AVS_UNIT_ASSERT_NOT_NULL(env->anjay);

    // Install Security, Server, and Access Control
    AVS_UNIT_ASSERT_SUCCESS(anjay_security_object_install(env->anjay));
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_object_install(env->anjay));
    AVS_UNIT_ASSERT_SUCCESS(anjay_access_control_install(env->anjay));

    // Install dummy objects
    AVS_UNIT_ASSERT_SUCCESS(anjay_register_object(env->anjay, &OBJ_DEF_1234));
    AVS_UNIT_ASSERT_SUCCESS(anjay_register_object(env->anjay, &OBJ_DEF_1235));
    AVS_UNIT_ASSERT_SUCCESS(anjay_register_object(env->anjay, &OBJ_DEF_1236));

    // Create Server and Security instances for SSID 1 and SSID 2
    // We need more than one server instance to prevent
    // `is_single_ssid_environment()` from returning true, which would bypass
    // some AC checks.
    add_server_and_security_1(env);
    add_server_and_security_2(env);
    return env;
}

static void core_access_test_env_destroy(core_access_test_env_t **env) {
    anjay_delete((*env)->anjay);
    avs_free(*env);
}

#    define SCOPED_CORE_ACCESS_TEST_ENV(Name)                            \
        SCOPED_PTR(core_access_test_env_t, core_access_test_env_destroy) \
        Name = core_access_test_env_create();

static anjay_notify_queue_object_entry_t *
create_add_entry(anjay_oid_t oid, const char *prefix, anjay_iid_t added_iid) {
    (void) prefix;
    anjay_notify_queue_object_entry_t *entry =
            AVS_LIST_NEW_ELEMENT(anjay_notify_queue_object_entry_t);
    AVS_UNIT_ASSERT_NOT_NULL(entry);
    entry->oid = oid;
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    if (prefix) {
        snprintf(entry->prefix, sizeof(entry->prefix), "%s", prefix);
    } else {
        entry->prefix[0] = '\0';
    }
#    endif // ANJAY_WITH_LWM2M_GATEWAY
    entry->instance_set_changes.instance_set_changed = true;
    anjay_iid_t *iid_ptr = AVS_LIST_NEW_ELEMENT(anjay_iid_t);
    AVS_UNIT_ASSERT_NOT_NULL(iid_ptr);
    *iid_ptr = added_iid;
    AVS_LIST_APPEND(&entry->instance_set_changes.known_added_iids, iid_ptr);
    return entry;
}

static int count_ac_instances(anjay_t *anjay, anjay_oid_t target_oid) {
    int count = 0;
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    const anjay_dm_installed_object_t *ac_obj =
            get_access_control(anjay_unlocked);
    // All tests add instance 1, so we check if AC instance exists for
    // target_oid and IID 1
    if (find_ac_instance_by_target(anjay_unlocked, ac_obj, NULL, target_oid, 1)
            == 0) {
        count = 1;
    } else {
        count = 0;
    }
    ANJAY_MUTEX_UNLOCK(anjay);
    return count;
}

// TEST: Access Control creation - base case (no prefixes).
// All 3 elements in the queue specify newly added instances of regular objects,
// without any gateway prefix. They should all be parsed by what_changed() and
// result in perform_adds() creating Access Control instances for each of them.
AVS_UNIT_TEST(anjay_sync_access_control, adds_3_elements_no_prefix) {
    SCOPED_CORE_ACCESS_TEST_ENV(env);
    anjay_t *anjay = env->anjay;

    anjay_notify_queue_t queue = NULL;
    AVS_LIST_APPEND(&queue, create_add_entry(1234, NULL, 1));
    AVS_LIST_APPEND(&queue, create_add_entry(1235, NULL, 1));
    AVS_LIST_APPEND(&queue, create_add_entry(1236, NULL, 1));

    // Using Origin SSID = 1 to differentiate from BOOTSTRAP.
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    AVS_UNIT_ASSERT_TRUE(_anjay_sync_access_control(anjay_unlocked, 1, &queue)
                         == 0);
    ANJAY_MUTEX_UNLOCK(anjay);

    // AC instances should be generated for all 3 target objects.
    AVS_UNIT_ASSERT_EQUAL(count_ac_instances(anjay, 1234), 1);
    AVS_UNIT_ASSERT_EQUAL(count_ac_instances(anjay, 1235), 1);
    AVS_UNIT_ASSERT_EQUAL(count_ac_instances(anjay, 1236), 1);

    _anjay_notify_clear_queue(&queue);
}

// TEST: Access Control creation bypass for gateways.
// The first element in the queue has a gateway prefix ("gw-").
// what_changed() should skip generating AC instance for this specific entry.
// For the 2nd and 3rd instances, AC creation should execute normally.
AVS_UNIT_TEST(anjay_sync_access_control, adds_1st_element_prefix) {
    SCOPED_CORE_ACCESS_TEST_ENV(env);
    anjay_t *anjay = env->anjay;

    anjay_notify_queue_t queue = NULL;
    AVS_LIST_APPEND(&queue, create_add_entry(1234, "gw-", 1));
    AVS_LIST_APPEND(&queue, create_add_entry(1235, NULL, 1));
    AVS_LIST_APPEND(&queue, create_add_entry(1236, NULL, 1));

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_sync_access_control(anjay_unlocked, 1, &queue));
    ANJAY_MUTEX_UNLOCK(anjay);

    int expected_count = 1;
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    // Prefix "gw-" on 1st element excludes it from perform_adds().
    expected_count = 0;
#    endif // ANJAY_WITH_LWM2M_GATEWAY

    AVS_UNIT_ASSERT_EQUAL(count_ac_instances(anjay, 1234), expected_count);
    AVS_UNIT_ASSERT_EQUAL(count_ac_instances(anjay, 1235), 1);
    AVS_UNIT_ASSERT_EQUAL(count_ac_instances(anjay, 1236), 1);

    _anjay_notify_clear_queue(&queue);
}

// TEST: Access Control creation bypass for gateways (middle element).
// Demonstrates that what_changed() ignores only the entry that actually has the
// prefix, meaning that elements before and after it are processed.
AVS_UNIT_TEST(anjay_sync_access_control, adds_2nd_element_prefix) {
    SCOPED_CORE_ACCESS_TEST_ENV(env);
    anjay_t *anjay = env->anjay;

    anjay_notify_queue_t queue = NULL;
    AVS_LIST_APPEND(&queue, create_add_entry(1234, NULL, 1));
    AVS_LIST_APPEND(&queue, create_add_entry(1235, "gw-", 1));
    AVS_LIST_APPEND(&queue, create_add_entry(1236, NULL, 1));

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    AVS_UNIT_ASSERT_TRUE(_anjay_sync_access_control(anjay_unlocked, 1, &queue)
                         == 0);
    ANJAY_MUTEX_UNLOCK(anjay);

    int expected_count = 1;
#    ifdef ANJAY_WITH_LWM2M_GATEWAY
    // Prefix "gw-" on 2nd element excludes it from perform_adds().
    expected_count = 0;
#    endif // ANJAY_WITH_LWM2M_GATEWAY

    AVS_UNIT_ASSERT_EQUAL(count_ac_instances(anjay, 1234), 1);
    AVS_UNIT_ASSERT_EQUAL(count_ac_instances(anjay, 1235), expected_count);
    AVS_UNIT_ASSERT_EQUAL(count_ac_instances(anjay, 1236), 1);

    _anjay_notify_clear_queue(&queue);
}

#endif // ANJAY_WITH_ACCESS_CONTROL
