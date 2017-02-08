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

#ifndef ANJAY_TEST_DM_H
#define	ANJAY_TEST_DM_H

#include <avsystem/commons/unit/mocksock.h>

#include <anjay_modules/utils.h>

#include <anjay_test/mock_clock.h>
#include <anjay_test/mock_dm.h>

anjay_t *_anjay_test_dm_init(void);

void _anjay_test_dm_unsched_reload_sockets(anjay_t *anjay);

avs_net_abstract_socket_t *_anjay_test_dm_install_socket(anjay_t *anjay,
                                                         anjay_ssid_t ssid);
void _anjay_test_dm_finish(anjay_t *anjay);

int _anjay_test_dm_fake_security_instance_it(anjay_t *anjay,
                                             const anjay_dm_object_def_t *const *obj_ptr,
                                             anjay_iid_t *out,
                                             void **cookie);

int _anjay_test_dm_fake_security_present(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj_ptr,
                                         anjay_iid_t iid,
                                         anjay_rid_t rid);

int _anjay_test_dm_fake_security_read(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj_ptr,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid,
                                      anjay_output_ctx_t *ctx);

static inline int
_anjay_test_dm_instance_reset_NOOP(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;
    return 0;
}

static const anjay_dm_object_def_t *const OBJ =
        &(const anjay_dm_object_def_t) {
            .oid = 42,
            .rid_bound = 7,
            ANJAY_MOCK_DM_HANDLERS,
            .instance_reset = _anjay_test_dm_instance_reset_NOOP
        };

static const anjay_dm_object_def_t *const OBJ_WITH_RESET =
        &(const anjay_dm_object_def_t) {
            .oid = 25,
            .rid_bound = 7,
            ANJAY_MOCK_DM_HANDLERS,
            .instance_reset = _anjay_mock_dm_instance_reset
        };

static anjay_dm_object_def_t *const EXECUTE_OBJ =
        &(anjay_dm_object_def_t) {
            .oid = 128,
            .rid_bound = 7,
            ANJAY_MOCK_DM_HANDLERS
        };

static const anjay_dm_object_def_t *const FAKE_SECURITY =
        &(const anjay_dm_object_def_t) {
            .oid = 0,
            .rid_bound = 11,
            .instance_it = _anjay_test_dm_fake_security_instance_it,
            .resource_supported = anjay_dm_resource_supported_TRUE,
            .resource_present = _anjay_test_dm_fake_security_present,
            .resource_read = _anjay_test_dm_fake_security_read,
            .transaction_begin = anjay_dm_transaction_NOOP,
            .transaction_validate = anjay_dm_transaction_NOOP,
            .transaction_commit = anjay_dm_transaction_NOOP,
            .transaction_rollback = anjay_dm_transaction_NOOP
        };

static const anjay_dm_object_def_t *const FAKE_SECURITY2 =
        &(const anjay_dm_object_def_t) {
            .oid = 0,
            .rid_bound = 11,
            ANJAY_MOCK_DM_HANDLERS
        };

static const anjay_dm_object_def_t *const FAKE_SERVER =
        &(const anjay_dm_object_def_t) {
            .oid = 1,
            .rid_bound = 9,
            ANJAY_MOCK_DM_HANDLERS
        };

static const anjay_dm_object_def_t *const OBJ_WITH_RES_OPS =
        &(const anjay_dm_object_def_t) {
            .oid = 667,
            .rid_bound = 8,
            ANJAY_MOCK_DM_HANDLERS,
            .resource_operations = _anjay_mock_dm_resource_operations
        };

#define DM_TEST_ARRAY(...) { __VA_ARGS__ }

#define DM_TEST_INIT_GENERIC(Objects, Ssids) \
    _anjay_mock_clock_start(&(const struct timespec) { 1000, 0 }); \
    anjay_t *anjay = _anjay_test_dm_init(); \
    const anjay_dm_object_def_t *const *obj_defs[] = DM_TEST_ARRAY Objects; \
    for (size_t i = 0; i < ANJAY_ARRAY_SIZE(obj_defs); ++i) { \
        AVS_UNIT_ASSERT_SUCCESS(anjay_register_object(anjay, obj_defs[i])); \
    } \
    anjay_ssid_t ssids[] = DM_TEST_ARRAY Ssids; \
    avs_net_abstract_socket_t *mocksocks[ANJAY_ARRAY_SIZE(ssids)]; \
    for (size_t i = ANJAY_ARRAY_SIZE(ssids) - 1; \
            i < ANJAY_ARRAY_SIZE(ssids); --i) { \
        mocksocks[i] = _anjay_test_dm_install_socket(anjay, ssids[i]); \
        avs_unit_mocksock_enable_recv_timeout_getsetopt(mocksocks[i], 1000); \
        avs_unit_mocksock_enable_inner_mtu_getopt(mocksocks[i], 1252); \
    } \
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay)); \
    _anjay_test_dm_unsched_reload_sockets(anjay)

#define DM_TEST_DEFAULT_OBJECTS \
        &OBJ, \
        &FAKE_SECURITY, \
        &FAKE_SERVER, \
        (const anjay_dm_object_def_t *const *) &OBJ_WITH_RES_OPS, \
        (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, \
        (const anjay_dm_object_def_t *const *) &OBJ_WITH_RESET

#define DM_TEST_INIT_WITH_OBJECTS(...) \
        DM_TEST_INIT_GENERIC( ( __VA_ARGS__ ), ( 1 ) )

#define DM_TEST_INIT_WITH_SSIDS(...) \
        DM_TEST_INIT_GENERIC( ( DM_TEST_DEFAULT_OBJECTS ), ( __VA_ARGS__ ) )

#define DM_TEST_INIT DM_TEST_INIT_WITH_SSIDS(1)

#define DM_TEST_FINISH _anjay_test_dm_finish(anjay)

#define DM_TEST_EXPECT_RESPONSE(Mocksock, Response) \
    avs_unit_mocksock_expect_output(Mocksock, Response, sizeof(Response) - 1)

#define DM_TEST_EXPECT_READ_NULL_ATTRS(Ssid, Iid, Rid) do { \
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, Iid, 1); \
    if (Rid >= 0) { \
        _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, (anjay_rid_t) Rid, \
                                                 1); \
        _anjay_mock_dm_expect_resource_present(anjay, &OBJ, Iid, (anjay_rid_t) Rid, \
                                               1); \
        _anjay_mock_dm_expect_resource_read_attrs( \
                anjay, &OBJ, Iid, (anjay_rid_t) Rid, Ssid, 0, \
                &ANJAY_DM_ATTRIBS_EMPTY); \
    } \
    _anjay_mock_dm_expect_instance_read_default_attrs( \
            anjay, &OBJ, Iid, Ssid, 0, &ANJAY_DM_ATTRIBS_EMPTY); \
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, Ssid, 0, \
                                                    &ANJAY_DM_ATTRIBS_EMPTY); \
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0, \
                                      ANJAY_IID_INVALID); \
} while (0)

#endif	/* ANJAY_TEST_DM_H */

