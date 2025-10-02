/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_mocksock.h>
#include <avsystem/commons/avs_unit_test.h>

#include "tests/utils/dm.h"

static const anjay_dm_object_def_t *const OBJ2 =
        &(const anjay_dm_object_def_t) {
            .oid = 69,
            .version = "21.37",
            .handlers = { ANJAY_MOCK_DM_HANDLERS_BASIC }
        };

static const anjay_dm_object_def_t *const FAKE_SERVER_WITH_VER =
        &(const anjay_dm_object_def_t) {
            .oid = 1,
            .version = "1.1",
            .handlers = { ANJAY_MOCK_DM_HANDLERS }
        };

#define PREPARE_DM()                                                         \
    /* Security and OSCORE objects should be omitted */                      \
    _anjay_mock_dm_expect_list_instances(                                    \
            anjay, &FAKE_SERVER_WITH_VER, 0,                                 \
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });         \
    _anjay_mock_dm_expect_list_instances(anjay, &OBJ_WITH_RESET, 0,          \
                                         (const anjay_iid_t[]) {             \
                                                 ANJAY_ID_INVALID });        \
    _anjay_mock_dm_expect_list_instances(                                    \
            anjay, &OBJ, 0, (const anjay_iid_t[]) { 14, ANJAY_ID_INVALID }); \
    _anjay_mock_dm_expect_list_instances(                                    \
            anjay, &OBJ2, 0,                                                 \
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });         \
    _anjay_mock_dm_expect_list_instances(                                    \
            anjay, (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ, 0,   \
            (const anjay_iid_t[]) { 14, 42, 69, ANJAY_ID_INVALID });

AVS_UNIT_TEST(io_corelnk, test_corelnk_output) {
    DM_TEST_INIT_WITH_OBJECTS(
            &OBJ2, &OBJ, &FAKE_SECURITY, &FAKE_SERVER_WITH_VER,
            (const anjay_dm_object_def_t *const *) &EXECUTE_OBJ,
            (const anjay_dm_object_def_t *const *) &OBJ_WITH_RESET);

    char *buf = NULL;
    PREPARE_DM();

    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_corelnk_query_dm(anjay_unlocked, &anjay_unlocked->dm,
                                    ANJAY_LWM2M_VERSION_1_0, &buf));
    ANJAY_MUTEX_UNLOCK(anjay);
    AVS_UNIT_ASSERT_EQUAL_STRING(
            buf,
            "</1>;ver=\"1.1\",</1/14>,</1/42>,</1/69>,</25>,</42/14>,</"
            "69>;ver=\"21.37\",</69/"
            "14>,</69/42>,</69/69>,</128/14>,</128/42>,</128/69>");
    avs_free(buf);
    buf = NULL;
#ifdef ANJAY_WITH_LWM2M11
    PREPARE_DM();
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_corelnk_query_dm(anjay_unlocked, &anjay_unlocked->dm,
                                    ANJAY_LWM2M_VERSION_1_1, &buf));
    ANJAY_MUTEX_UNLOCK(anjay);

    // both versions are valid
    char *with_version = "</1>;ver=1.1,</1/14>,</1/42>,</1/69>,</25>,</42/"
                         "14>,</69>;ver=21.37,</69/"
                         "14>,</69/42>,</69/69>,</128/14>,</128/42>,</128/69>";
    char *without_version =
            "</1/14>,</1/42>,</1/69>,</25>,</42/14>,</69>;ver=21.37,</69/"
            "14>,</69/42>,</69/69>,</128/14>,</128/42>,</128/69>";
    ASSERT_TRUE(strcmp(buf, with_version) == 0
                || strcmp(buf, without_version) == 0);
    avs_free(buf);
    buf = NULL;
#endif // ANJAY_WITH_LWM2M11

    DM_TEST_FINISH;
    avs_free(buf);
}
