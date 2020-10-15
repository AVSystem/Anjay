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

#include <anjay_init.h>

#define AVS_UNIT_ENABLE_SHORT_ASSERTS
#include <avsystem/commons/avs_unit_test.h>

#include <stdarg.h>
#include <stdio.h>

#include <avsystem/coap/ctx.h>

#include "src/core/servers/anjay_server_connections.h"
#include "tests/core/coap/utils.h"
#include "tests/utils/dm.h"
#include "tests/utils/utils.h"

AVS_UNIT_GLOBAL_INIT(verbose) {
#if defined(AVS_COMMONS_WITH_AVS_LOG) && defined(ANJAY_WITH_LOGS)
    if (verbose < 2) {
        avs_log_set_default_level(AVS_LOG_QUIET);
    }
#endif
}

#define TEST_NULLABLE_STRING_EQUAL(Actual, Expected) \
    do {                                             \
        if (Expected != NULL) {                      \
            ASSERT_NOT_NULL((Actual));               \
            ASSERT_EQ_STR((Actual), (Expected));     \
        } else {                                     \
            ASSERT_NULL((Actual));                   \
        }                                            \
    } while (0)

#define TEST_SPLIT_QUERY_STRING(QueryString, ExpectedKey, ExpectedValue) \
    do {                                                                 \
        char buf[] = QueryString;                                        \
        const char *key;                                                 \
        const char *value;                                               \
        split_query_string(buf, &key, &value);                           \
        TEST_NULLABLE_STRING_EQUAL(key, ExpectedKey);                    \
        TEST_NULLABLE_STRING_EQUAL(value, ExpectedValue);                \
    } while (0)

AVS_UNIT_TEST(parse_headers, split_query_string) {
    TEST_SPLIT_QUERY_STRING("", "", NULL);
    TEST_SPLIT_QUERY_STRING("key", "key", NULL);
    TEST_SPLIT_QUERY_STRING("key=", "key", "");
    TEST_SPLIT_QUERY_STRING("=value", "", "value");
    TEST_SPLIT_QUERY_STRING("key=value", "key", "value");
}

#undef TEST_SPLIT_QUERY_STRING
#undef TEST_NULLABLE_STRING_EQUAL

#define TEST_PARSE_ATTRIBUTE_SUCCESS(Key, Value, ExpectedField,       \
                                     ExpectedHasField, ExpectedValue) \
    do {                                                              \
        anjay_request_attributes_t attrs;                             \
        memset(&attrs, 0, sizeof(attrs));                             \
        ASSERT_OK(parse_attribute(&attrs, (Key), (Value)));           \
        ASSERT_EQ(attrs.values.ExpectedField, (ExpectedValue));       \
        anjay_request_attributes_t expected;                          \
        memset(&expected, 0, sizeof(expected));                       \
        expected.ExpectedHasField = true;                             \
        expected.values.ExpectedField = (ExpectedValue);              \
        ASSERT_EQ_BYTES_SIZED(&attrs, &expected,                      \
                              sizeof(anjay_request_attributes_t));    \
    } while (0)

#define TEST_PARSE_ATTRIBUTE_FAIL(Key, Value)                 \
    do {                                                      \
        anjay_request_attributes_t attrs;                     \
        memset(&attrs, 0, sizeof(attrs));                     \
        ASSERT_FAIL(parse_attribute(&attrs, (Key), (Value))); \
    } while (0);

AVS_UNIT_TEST(parse_headers, parse_attribute) {
    TEST_PARSE_ATTRIBUTE_SUCCESS("pmin", "123", standard.common.min_period,
                                 has_min_period, 123);
    TEST_PARSE_ATTRIBUTE_SUCCESS("pmin", NULL, standard.common.min_period,
                                 has_min_period, -1);
    TEST_PARSE_ATTRIBUTE_FAIL("pmin", "123.4");
    TEST_PARSE_ATTRIBUTE_FAIL("pmin", "woof");
    TEST_PARSE_ATTRIBUTE_FAIL("pmin", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("pmax", "234", standard.common.max_period,
                                 has_max_period, 234);
    TEST_PARSE_ATTRIBUTE_SUCCESS("pmax", NULL, standard.common.max_period,
                                 has_max_period, -1);
    TEST_PARSE_ATTRIBUTE_FAIL("pmax", "234.5");
    TEST_PARSE_ATTRIBUTE_FAIL("pmax", "meow");
    TEST_PARSE_ATTRIBUTE_FAIL("pmax", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("gt", "345", standard.greater_than,
                                 has_greater_than, 345.0);
    TEST_PARSE_ATTRIBUTE_SUCCESS("gt", "345.6", standard.greater_than,
                                 has_greater_than, 345.6);
    TEST_PARSE_ATTRIBUTE_SUCCESS("gt", NULL, standard.greater_than,
                                 has_greater_than, NAN);
    TEST_PARSE_ATTRIBUTE_FAIL("gt", "tweet");
    TEST_PARSE_ATTRIBUTE_FAIL("gt", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("lt", "456", standard.less_than, has_less_than,
                                 456.0);
    TEST_PARSE_ATTRIBUTE_SUCCESS("lt", "456.7", standard.less_than,
                                 has_less_than, 456.7);
    TEST_PARSE_ATTRIBUTE_SUCCESS("lt", NULL, standard.less_than, has_less_than,
                                 NAN);
    TEST_PARSE_ATTRIBUTE_FAIL("lt", "squeak");
    TEST_PARSE_ATTRIBUTE_FAIL("lt", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("st", "567", standard.step, has_step, 567.0);
    TEST_PARSE_ATTRIBUTE_SUCCESS("st", "567.8", standard.step, has_step, 567.8);
    TEST_PARSE_ATTRIBUTE_SUCCESS("st", NULL, standard.step, has_step, NAN);
    TEST_PARSE_ATTRIBUTE_FAIL("st", "moo");
    TEST_PARSE_ATTRIBUTE_FAIL("st", "");

    TEST_PARSE_ATTRIBUTE_FAIL("unknown", "wa-pa-pa-pa-pa-pa-pow");
    TEST_PARSE_ATTRIBUTE_FAIL("unknown", NULL);
    TEST_PARSE_ATTRIBUTE_FAIL("unknown", "");
}

#undef TEST_PARSE_ATTRIBUTE_SUCCESS
#undef TEST_PARSE_ATTRIBUTE_FAILED

#ifdef WITH_CUSTOM_ATTRIBUTES
#    define ASSERT_CUSTOM_ATTRIBUTE_VALUES_EQUAL(actual, expected) \
        ASSERT_EQ(actual.custom.data.con, expected.custom.data.con)
#else // WITH_CUSTOM_ATTRIBUTES
#    define ASSERT_CUSTOM_ATTRIBUTE_VALUES_EQUAL(actual, expected) ((void) 0)
#endif // WITH_CUSTOM_ATTRIBUTES

#define ASSERT_ATTRIBUTE_VALUES_EQUAL(actual, expected)                    \
    do {                                                                   \
        ASSERT_CUSTOM_ATTRIBUTE_VALUES_EQUAL(actual, expected);            \
        ASSERT_EQ(actual.standard.common.min_period,                       \
                  expected.standard.common.min_period);                    \
        ASSERT_EQ(actual.standard.common.max_period,                       \
                  expected.standard.common.max_period);                    \
        ASSERT_EQ(actual.standard.greater_than,                            \
                  expected.standard.greater_than);                         \
        ASSERT_EQ(actual.standard.less_than, expected.standard.less_than); \
        ASSERT_EQ(actual.standard.step, expected.standard.step);           \
    } while (0)

#ifdef WITH_CUSTOM_ATTRIBUTES
#    define ASSERT_CUSTOM_ATTRIBUTE_FLAGS_EQUAL(actual, expected) \
        ASSERT_EQ(actual.custom.has_con, expected.custom.has_con)
#else // WITH_CUSTOM_ATTRIBUTES
#    define ASSERT_CUSTOM_ATTRIBUTE_FLAGS_EQUAL(actual, expected) ((void) 0)
#endif // WITH_CUSTOM_ATTRIBUTES

#define ASSERT_ATTRIBUTES_EQUAL(actual, expected)                      \
    do {                                                               \
        ASSERT_EQ(actual.has_min_period, expected.has_min_period);     \
        ASSERT_EQ(actual.has_max_period, expected.has_max_period);     \
        ASSERT_EQ(actual.has_greater_than, expected.has_greater_than); \
        ASSERT_EQ(actual.has_less_than, expected.has_less_than);       \
        ASSERT_EQ(actual.has_step, expected.has_step);                 \
        ASSERT_CUSTOM_ATTRIBUTE_FLAGS_EQUAL(actual, expected);         \
        ASSERT_ATTRIBUTE_VALUES_EQUAL(actual.values, expected.values); \
    } while (0)

typedef struct {
    uint8_t buffer[1024];
    avs_coap_request_header_t header;
} header_with_opts_storage_t;

static avs_coap_request_header_t *header_with_string_opts(
        header_with_opts_storage_t *storage, uint16_t string_option, ...) {
    memset(storage, 0, sizeof(*storage));
    storage->header.options =
            avs_coap_options_create_empty(storage->buffer,
                                          sizeof(storage->buffer));
    va_list query;
    va_start(query, string_option);
    const char *arg = NULL;
    while ((arg = va_arg(query, const char *))) {
        ASSERT_OK(avs_coap_options_add_string(&storage->header.options,
                                              string_option, arg));
    }
    va_end(query);
    return &storage->header;
}

AVS_UNIT_TEST(parse_headers, parse_attributes) {
    anjay_request_attributes_t attrs;
    anjay_request_attributes_t empty_attrs;
    memset(&empty_attrs, 0, sizeof(empty_attrs));
    empty_attrs.values = ANJAY_DM_INTERNAL_R_ATTRS_EMPTY;
    anjay_request_attributes_t expected_attrs;
    header_with_opts_storage_t header_storage;

    // no query-strings
    ASSERT_OK(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    NULL),
            &attrs));
    ASSERT_EQ_BYTES_SIZED(&attrs, &empty_attrs, sizeof(attrs));

    // single query-string
    memcpy(&expected_attrs, &empty_attrs, sizeof(expected_attrs));
    expected_attrs.has_min_period = true;
    expected_attrs.values.standard.common.min_period = 10;
    ASSERT_OK(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "pmin=10", NULL),
            &attrs));
    ASSERT_ATTRIBUTES_EQUAL(attrs, expected_attrs);

    // multiple query-strings
    memcpy(&expected_attrs, &empty_attrs, sizeof(expected_attrs));
    expected_attrs.has_min_period = true;
    expected_attrs.values.standard.common.min_period = 10;
    expected_attrs.has_max_period = true;
    expected_attrs.values.standard.common.max_period = 20;
    ASSERT_OK(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "pmin=10", "pmax=20", NULL),
            &attrs));
    ASSERT_ATTRIBUTES_EQUAL(attrs, expected_attrs);

    // duplicate options
    ASSERT_FAIL(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "pmin=10", "pmin=20", NULL),
            &attrs));

    ASSERT_FAIL(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "lt=4", "lt=6", NULL),
            &attrs));

    // unrecognized query-string only
    ASSERT_FAIL(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "WhatsTheMeaningOf=Stonehenge", NULL),
            &attrs));

    // unrecognized query-string first
    ASSERT_FAIL(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "WhyDidTheyBuildThe=Stonehenge", "pmax=20",
                                    NULL),
            &attrs));

    // unrecognized query-string last
    ASSERT_FAIL(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "gt=30.5", "AllICanThinkOfIsStonehenge",
                                    NULL),
            &attrs));

    // multiple unrecognized query-strings
    ASSERT_FAIL(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "Stonehenge", "Stonehenge",
                                    "LotsOfStonesInARow", NULL),
            &attrs));

    // single query-string among multiple unrecognized ones
    ASSERT_FAIL(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "TheyWere=25Tons", "EachStoneMyFriend",
                                    "lt=40.5", "ButAmazinglyThey",
                                    "GotThemAllDownInTheSand", NULL),
            &attrs));

    // invalid query-string value
    ASSERT_FAIL(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "st=What'sTheDealWithStonehenge", NULL),
            &attrs));

    // unexpected value
    ASSERT_FAIL(parse_attributes(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "YouShouldHaveLeftATinyHint", NULL),
            &attrs));
}

#undef ASSERT_ATTRIBUTES_EQUAL
#undef ASSERT_ATTRIBUTE_VALUES_EQUAL

AVS_UNIT_TEST(parse_headers, parse_uri) {
    bool is_bs;
    anjay_uri_path_t uri;
    header_with_opts_storage_t header_storage;

    // OID only
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "1", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &MAKE_OBJECT_PATH(1)));

    // OID+IID
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "2", "3", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &MAKE_INSTANCE_PATH(2, 3)));

    // OID+IID+RID
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "4", "5", "6", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &MAKE_RESOURCE_PATH(4, 5, 6)));

    // OID+IID+RID+RIID
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "7", "8", "9", "10", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    ASSERT_TRUE(_anjay_uri_path_equal(
            &uri, &MAKE_RESOURCE_INSTANCE_PATH(7, 8, 9, 10)));

    // max valid OID/IID/RID/RIID
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "65534", "65534", "65534", "65534", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    ASSERT_TRUE(_anjay_uri_path_equal(
            &uri, &MAKE_RESOURCE_INSTANCE_PATH(65534, 65534, 65534, 65534)));

    // Bootstrap URI
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "bs", NULL),
            &is_bs, &uri));
    ASSERT_TRUE(is_bs);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &MAKE_ROOT_PATH()));

    // no Request-Uri
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &MAKE_ROOT_PATH()));

    // empty Request-Uri - permitted alternate form
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &MAKE_ROOT_PATH()));

    // superfluous empty segments
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "", "1", NULL),
            &is_bs, &uri));
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "1", "", "2", NULL),
            &is_bs, &uri));

    // prefix
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "they're taking the hobbits", "to isengard",
                                    "7", "8", "9", NULL),
            &is_bs, &uri));

    // prefix that looks like OID + OID+IID+RID+RIID
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "100", "10", "11", "12", "13", NULL),
            &is_bs, &uri));

    // prefix that looks like OID/IID/RID + string + OID only
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "100", "101", "102", "wololo", "13", NULL),
            &is_bs, &uri));

    // trailing non-numeric segment
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "14", "NopeChuckTesta", NULL),
            &is_bs, &uri));

    // invalid OID
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "65535", NULL),
            &is_bs, &uri));

    // invalid IID
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "15", "65535", NULL),
            &is_bs, &uri));

    // invalid RID
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "16", "17", "65535", NULL),
            &is_bs, &uri));

    // invalid RIID
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "16", "17", "18", "65535", NULL),
            &is_bs, &uri));

    // BS and something more
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "bs", "1", "2", NULL),
            &is_bs, &uri));
}

AVS_UNIT_TEST(parse_headers, parse_action) {
    anjay_request_t request;
    memset(&request, 0, sizeof(request));
    request.content_format = AVS_COAP_FORMAT_NONE;
    request.request_code = AVS_COAP_CODE_GET;

    ASSERT_OK(parse_action(&(const avs_coap_request_header_t) {
                               .code = AVS_COAP_CODE_GET
                           },
                           &request));
    ASSERT_EQ(request.action, ANJAY_ACTION_READ);

    request.request_code = AVS_COAP_CODE_GET;
    char option_buffer[128];
    avs_coap_request_header_t header_with_accept = {
        .code = AVS_COAP_CODE_GET,
        .options = avs_coap_options_create_empty(option_buffer,
                                                 sizeof(option_buffer))
    };
    ASSERT_OK(avs_coap_options_add_u16(&header_with_accept.options,
                                       AVS_COAP_OPTION_ACCEPT,
                                       AVS_COAP_FORMAT_LINK_FORMAT));
    ASSERT_OK(parse_action(&header_with_accept, &request));
    ASSERT_EQ(request.action, ANJAY_ACTION_DISCOVER);

    request.request_code = AVS_COAP_CODE_POST;
    request.uri = MAKE_RESOURCE_PATH(0, 0, 0);
    ASSERT_OK(parse_action(&(const avs_coap_request_header_t) {
                               .code = AVS_COAP_CODE_GET
                           },
                           &request));
    ASSERT_EQ(request.action, ANJAY_ACTION_EXECUTE);

    request.request_code = AVS_COAP_CODE_POST;
    request.uri = MAKE_OBJECT_PATH(0);
    request.content_format = AVS_COAP_FORMAT_PLAINTEXT;
    ASSERT_OK(parse_action(&(const avs_coap_request_header_t) {
                               .code = AVS_COAP_CODE_GET
                           },
                           &request));
    ASSERT_EQ(request.action, ANJAY_ACTION_CREATE);

    request.request_code = AVS_COAP_CODE_POST;
    request.uri = MAKE_INSTANCE_PATH(0, 0);
    request.content_format = AVS_COAP_FORMAT_OMA_LWM2M_TLV;
    ASSERT_OK(parse_action(&(const avs_coap_request_header_t) {
                               .code = AVS_COAP_CODE_GET
                           },
                           &request));
    ASSERT_EQ(request.action, ANJAY_ACTION_WRITE_UPDATE);

    request.request_code = AVS_COAP_CODE_PUT;
    request.content_format = AVS_COAP_FORMAT_NONE;
    ASSERT_OK(parse_action(&(const avs_coap_request_header_t) {
                               .code = AVS_COAP_CODE_GET
                           },
                           &request));
    ASSERT_EQ(request.action, ANJAY_ACTION_WRITE_ATTRIBUTES);

    request.request_code = AVS_COAP_CODE_PUT;
    request.content_format = AVS_COAP_FORMAT_PLAINTEXT;
    ASSERT_OK(parse_action(&(const avs_coap_request_header_t) {
                               .code = AVS_COAP_CODE_GET
                           },
                           &request));
    ASSERT_EQ(request.action, ANJAY_ACTION_WRITE);

    request.request_code = AVS_COAP_CODE_DELETE;
    ASSERT_OK(parse_action(&(const avs_coap_request_header_t) {
                               .code = AVS_COAP_CODE_GET
                           },
                           &request));
    ASSERT_EQ(request.action, ANJAY_ACTION_DELETE);

    request.request_code = AVS_COAP_CODE_NOT_FOUND;
    ASSERT_FAIL(parse_action(&(const avs_coap_request_header_t) {
                                 .code = AVS_COAP_CODE_GET
                             },
                             &request));
}

AVS_UNIT_TEST(queue_mode, change) {
    DM_TEST_INIT_WITH_OBJECTS(&OBJ, &FAKE_SECURITY2, &FAKE_SERVER);
    anjay_server_connection_t *connection =
            _anjay_get_server_connection((const anjay_connection_ref_t) {
                .server = anjay->servers->servers,
                .conn_type = ANJAY_CONNECTION_PRIMARY
            });
    ASSERT_NOT_NULL(connection);
    ////// WRITE NEW BINDING //////
    // Write to Binding - dummy data to assert it is actually queried via Read
    DM_TEST_REQUEST(mocksocks[0], CON, PUT, ID(0xFA3E), PATH("1", "1", "7"),
                    CONTENT_FORMAT(PLAINTEXT), PAYLOAD("dummy"));
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_LIFETIME, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMIN, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMAX, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_NOTIFICATION_STORING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_write(anjay, &FAKE_SERVER, 1,
                                         ANJAY_DM_RID_SERVER_BINDING,
                                         ANJAY_ID_INVALID,
                                         ANJAY_MOCK_DM_STRING(0, "dummy"), 0);
    // SSID will be read afterwards
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_LIFETIME, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMIN, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMAX, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_NOTIFICATION_STORING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    ASSERT_OK(anjay_serve(anjay, mocksocks[0]));

    {
        AVS_LIST(avs_net_socket_t *const) sockets = anjay_get_sockets(anjay);
        ASSERT_NOT_NULL(sockets);
        ASSERT_EQ(AVS_LIST_SIZE(sockets), 1);
        avs_net_socket_t *socket = *sockets;

        AVS_LIST(const anjay_socket_entry_t) entries =
                anjay_get_socket_entries(anjay);
        ASSERT_NOT_NULL(entries);
        ASSERT_EQ(AVS_LIST_SIZE(entries), 1);
        ASSERT_TRUE(entries->socket == socket);
        ASSERT_EQ(entries->transport, ANJAY_SOCKET_TRANSPORT_UDP);
        ASSERT_EQ(entries->ssid, 1);
        ASSERT_FALSE(entries->queue_mode);
    }
    ASSERT_NULL(connection->queue_mode_close_socket_clb);

    ////// REFRESH BINDING MODE //////
    // query SSID in Server
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_LIFETIME, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMIN, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMAX, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_NOTIFICATION_STORING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    // get Binding
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_LIFETIME, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMIN, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMAX, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_NOTIFICATION_STORING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_BINDING,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "UQ"));
    // query SSID in Security
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SECURITY2, 0,
            (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SECURITY2, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SECURITY_SERVER_URI, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SECURITY_BOOTSTRAP, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SECURITY_MODE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_BOOTSTRAP,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_BOOL(0, false));

    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SECURITY2, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SECURITY_SERVER_URI, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SECURITY_BOOTSTRAP, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SECURITY_MODE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    // get URI
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SECURITY2, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SECURITY_SERVER_URI, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SECURITY_BOOTSTRAP, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SECURITY_MODE, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SECURITY_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(
            anjay, &FAKE_SECURITY2, 1, ANJAY_DM_RID_SECURITY_SERVER_URI,
            ANJAY_ID_INVALID, 0, ANJAY_MOCK_DM_STRING(0, "coap://127.0.0.1"));

    // data model for the Update message - just fake an empty one
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0, (const anjay_iid_t[]) { ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_instances(
            anjay, &OBJ, 0, (const anjay_iid_t[]) { ANJAY_ID_INVALID });
    // lifetime
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_LIFETIME, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMIN, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMAX, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_NOTIFICATION_STORING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    _anjay_mock_dm_expect_list_resources(
            anjay, &FAKE_SERVER, 1, 0,
            (const anjay_mock_dm_res_entry_t[]) {
                    { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_LIFETIME, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMIN, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_DEFAULT_PMAX, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_NOTIFICATION_STORING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_ABSENT },
                    { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW,
                      ANJAY_DM_RES_PRESENT },
                    ANJAY_MOCK_DM_RES_END });
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_LIFETIME,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 9001));
    const coap_test_msg_t *update =
            COAP_MSG(CON, POST, ID_TOKEN_RAW(0x0000, nth_token(0)),
                     CONTENT_FORMAT(LINK_FORMAT), QUERY("lt=9001", "b=UQ"),
                     PAYLOAD("</1>,</42>"));
    avs_unit_mocksock_expect_output(mocksocks[0], update->content,
                                    update->length);
    while (anjay_sched_calculate_wait_time_ms(anjay, INT_MAX) == 0) {
        anjay_sched_run(anjay);
    }

    const coap_test_msg_t *update_response =
            COAP_MSG(ACK, CHANGED, ID_TOKEN_RAW(0x0000, nth_token(0)),
                     NO_PAYLOAD);
    avs_unit_mocksock_input(mocksocks[0], update_response->content,
                            update_response->length);
    ASSERT_OK(anjay_serve(anjay, mocksocks[0]));

    ASSERT_NOT_NULL(connection->queue_mode_close_socket_clb);
    // After 93s from now, the socket should be closed. We first wait for 92s,
    // ensure that the socket is still not closed, then wait one more second,
    // and ensure that the socket got closed.
    _anjay_mock_clock_advance(avs_time_duration_from_scalar(92, AVS_TIME_S));
    anjay_sched_run(anjay);
    ASSERT_NOT_NULL(connection->queue_mode_close_socket_clb);

    _anjay_mock_clock_advance(avs_time_duration_from_scalar(1, AVS_TIME_S));
    avs_unit_mocksock_expect_shutdown(mocksocks[0]);
    anjay_sched_run(anjay);

    ASSERT_NULL(anjay_get_sockets(anjay));
    ASSERT_NULL(anjay_get_socket_entries(anjay));
    ASSERT_NULL(connection->queue_mode_close_socket_clb);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(anjay_new, no_endpoint_name) {
    const anjay_configuration_t configuration = {
        .endpoint_name = NULL,
        .in_buffer_size = 4096,
        .out_buffer_size = 4096
    };
    ASSERT_NULL(anjay_new(&configuration));
}
