/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
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

#include <avsystem/coap/ctx.h>

#include <anjay_modules/anjay_dm_utils.h>

#include "src/core/anjay_servers_inactive.h"
#include "src/core/anjay_servers_reload.h"
#include "src/core/servers/anjay_server_connections.h"
#include "tests/core/coap/utils.h"
#include "tests/core/socket_mock.h"
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

#define PARSE_QUERY_WRAPPED(OutAttrs, OutDepth, Key, Value)               \
    ({                                                                    \
        int parse_query_result = parse_query((OutAttrs), (Key), (Value)); \
        *(OutDepth) = -1;                                                 \
        parse_query_result;                                               \
    })
#define PARSE_QUERIES_WRAPPED(Header, OutAttrs, OutDepth)               \
    ({                                                                  \
        int parse_queries_result = parse_queries((Header), (OutAttrs)); \
        *(OutDepth) = -1;                                               \
        parse_queries_result;                                           \
    })

#define TEST_PARSE_ATTRIBUTE_SUCCESS(Key, Value, ExpectedField,         \
                                     ExpectedHasField, ExpectedValue)   \
    do {                                                                \
        anjay_request_attributes_t attrs;                               \
        int8_t depth = -1;                                              \
        memset(&attrs, 0, sizeof(attrs));                               \
        ASSERT_OK(PARSE_QUERY_WRAPPED(&attrs, &depth, (Key), (Value))); \
        ASSERT_EQ(depth, -1);                                           \
        ASSERT_EQ(attrs.values.ExpectedField, (ExpectedValue));         \
        anjay_request_attributes_t expected;                            \
        memset(&expected, 0, sizeof(expected));                         \
        expected.ExpectedHasField = true;                               \
        expected.values.ExpectedField = (ExpectedValue);                \
        ASSERT_EQ_BYTES_SIZED(&attrs, &expected,                        \
                              sizeof(anjay_request_attributes_t));      \
    } while (0)

#define TEST_PARSE_ATTRIBUTE_FAIL(Key, Value)                             \
    do {                                                                  \
        anjay_request_attributes_t attrs;                                 \
        int8_t depth = -1;                                                \
        memset(&attrs, 0, sizeof(attrs));                                 \
        ASSERT_FAIL(PARSE_QUERY_WRAPPED(&attrs, &depth, (Key), (Value))); \
    } while (0);

AVS_UNIT_TEST(parse_headers, parse_attribute) {
    TEST_PARSE_ATTRIBUTE_SUCCESS("pmin", "123", common.min_period,
                                 has_min_period, 123);
    TEST_PARSE_ATTRIBUTE_SUCCESS("pmin", NULL, common.min_period,
                                 has_min_period, -1);
    TEST_PARSE_ATTRIBUTE_FAIL("pmin", "123.4");
    TEST_PARSE_ATTRIBUTE_FAIL("pmin", "woof");
    TEST_PARSE_ATTRIBUTE_FAIL("pmin", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("pmax", "234", common.max_period,
                                 has_max_period, 234);
    TEST_PARSE_ATTRIBUTE_SUCCESS("pmax", NULL, common.max_period,
                                 has_max_period, -1);
    TEST_PARSE_ATTRIBUTE_FAIL("pmax", "234.5");
    TEST_PARSE_ATTRIBUTE_FAIL("pmax", "meow");
    TEST_PARSE_ATTRIBUTE_FAIL("pmax", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("gt", "345", greater_than, has_greater_than,
                                 345.0);
    TEST_PARSE_ATTRIBUTE_SUCCESS("gt", "345.6", greater_than, has_greater_than,
                                 345.6);
    TEST_PARSE_ATTRIBUTE_SUCCESS("gt", NULL, greater_than, has_greater_than,
                                 NAN);
    TEST_PARSE_ATTRIBUTE_FAIL("gt", "tweet");
    TEST_PARSE_ATTRIBUTE_FAIL("gt", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("lt", "456", less_than, has_less_than, 456.0);
    TEST_PARSE_ATTRIBUTE_SUCCESS("lt", "456.7", less_than, has_less_than,
                                 456.7);
    TEST_PARSE_ATTRIBUTE_SUCCESS("lt", NULL, less_than, has_less_than, NAN);
    TEST_PARSE_ATTRIBUTE_FAIL("lt", "squeak");
    TEST_PARSE_ATTRIBUTE_FAIL("lt", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("st", "567", step, has_step, 567.0);
    TEST_PARSE_ATTRIBUTE_SUCCESS("st", "567.8", step, has_step, 567.8);
    TEST_PARSE_ATTRIBUTE_SUCCESS("st", NULL, step, has_step, NAN);
    TEST_PARSE_ATTRIBUTE_FAIL("st", "moo");
    TEST_PARSE_ATTRIBUTE_FAIL("st", "");

    TEST_PARSE_ATTRIBUTE_FAIL("unknown", "wa-pa-pa-pa-pa-pa-pow");
    TEST_PARSE_ATTRIBUTE_FAIL("unknown", NULL);
    TEST_PARSE_ATTRIBUTE_FAIL("unknown", "");
}

#undef TEST_PARSE_ATTRIBUTE_SUCCESS
#undef TEST_PARSE_ATTRIBUTE_FAILED

#ifdef ANJAY_WITH_CON_ATTR
#    define ASSERT_CON_ATTRIBUTE_VALUES_EQUAL(actual, expected) \
        ASSERT_EQ(actual.common.con, expected.common.con)
#else // ANJAY_WITH_CON_ATTR
#    define ASSERT_CON_ATTRIBUTE_VALUES_EQUAL(actual, expected) ((void) 0)
#endif // ANJAY_WITH_CON_ATTR

#define ASSERT_LWM2M12_ATTRIBUTE_VALUES_EQUAL(actual, expected) ((void) 0)

#define ASSERT_ATTRIBUTE_VALUES_EQUAL(actual, expected)                  \
    do {                                                                 \
        ASSERT_EQ(actual.common.min_period, expected.common.min_period); \
        ASSERT_EQ(actual.common.max_period, expected.common.max_period); \
        ASSERT_EQ(actual.common.min_eval_period,                         \
                  expected.common.min_eval_period);                      \
        ASSERT_EQ(actual.common.max_eval_period,                         \
                  expected.common.max_eval_period);                      \
        ASSERT_EQ(actual.greater_than, expected.greater_than);           \
        ASSERT_EQ(actual.less_than, expected.less_than);                 \
        ASSERT_EQ(actual.step, expected.step);                           \
        ASSERT_CON_ATTRIBUTE_VALUES_EQUAL(actual, expected);             \
        ASSERT_LWM2M12_ATTRIBUTE_VALUES_EQUAL(actual, expected);         \
    } while (0)

#ifdef ANJAY_WITH_CON_ATTR
#    define ASSERT_CON_ATTRIBUTE_FLAGS_EQUAL(actual, expected) \
        ASSERT_EQ(actual.has_con, expected.has_con)
#else // ANJAY_WITH_CON_ATTR
#    define ASSERT_CON_ATTRIBUTE_FLAGS_EQUAL(actual, expected) ((void) 0)
#endif // ANJAY_WITH_CON_ATTR

#define ASSERT_LWM2M12_ATTRIBUTE_FLAGS_EQUAL(actual, expected) ((void) 0)

#define ASSERT_ATTRIBUTES_EQUAL(actual, expected)                            \
    do {                                                                     \
        ASSERT_EQ(actual.has_min_period, expected.has_min_period);           \
        ASSERT_EQ(actual.has_max_period, expected.has_max_period);           \
        ASSERT_EQ(actual.has_min_eval_period, expected.has_min_eval_period); \
        ASSERT_EQ(actual.has_max_eval_period, expected.has_max_eval_period); \
        ASSERT_EQ(actual.has_greater_than, expected.has_greater_than);       \
        ASSERT_EQ(actual.has_less_than, expected.has_less_than);             \
        ASSERT_EQ(actual.has_step, expected.has_step);                       \
        ASSERT_CON_ATTRIBUTE_FLAGS_EQUAL(actual, expected);                  \
        ASSERT_LWM2M12_ATTRIBUTE_FLAGS_EQUAL(actual, expected);              \
        ASSERT_ATTRIBUTE_VALUES_EQUAL(actual.values, expected.values);       \
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
    int8_t depth;
    anjay_request_attributes_t empty_attrs;
    memset(&empty_attrs, 0, sizeof(empty_attrs));
    empty_attrs.values = ANJAY_DM_R_ATTRIBUTES_EMPTY;
    anjay_request_attributes_t expected_attrs;
    header_with_opts_storage_t header_storage;

    // no query-strings
    ASSERT_OK(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    NULL),
            &attrs, &depth));
    ASSERT_EQ_BYTES_SIZED(&attrs, &empty_attrs, sizeof(attrs));
    ASSERT_EQ(depth, -1);

    // single query-string
    memcpy(&expected_attrs, &empty_attrs, sizeof(expected_attrs));
    expected_attrs.has_min_period = true;
    expected_attrs.values.common.min_period = 10;
    ASSERT_OK(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "pmin=10", NULL),
            &attrs, &depth));
    ASSERT_ATTRIBUTES_EQUAL(attrs, expected_attrs);
    ASSERT_EQ(depth, -1);

    // multiple query-strings
    memcpy(&expected_attrs, &empty_attrs, sizeof(expected_attrs));
    expected_attrs.has_min_period = true;
    expected_attrs.values.common.min_period = 10;
    expected_attrs.has_max_period = true;
    expected_attrs.values.common.max_period = 20;
    ASSERT_OK(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "pmin=10", "pmax=20", NULL),
            &attrs, &depth));
    ASSERT_ATTRIBUTES_EQUAL(attrs, expected_attrs);
    ASSERT_EQ(depth, -1);

    // duplicate options
    ASSERT_FAIL(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "pmin=10", "pmin=20", NULL),
            &attrs, &depth));

    ASSERT_FAIL(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "lt=4", "lt=6", NULL),
            &attrs, &depth));

    // unrecognized query-string only
    ASSERT_FAIL(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "WhatsTheMeaningOf=Stonehenge", NULL),
            &attrs, &depth));

    // unrecognized query-string first
    ASSERT_FAIL(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "WhyDidTheyBuildThe=Stonehenge", "pmax=20",
                                    NULL),
            &attrs, &depth));

    // unrecognized query-string last
    ASSERT_FAIL(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "gt=30.5", "AllICanThinkOfIsStonehenge",
                                    NULL),
            &attrs, &depth));

    // multiple unrecognized query-strings
    ASSERT_FAIL(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "Stonehenge", "Stonehenge",
                                    "LotsOfStonesInARow", NULL),
            &attrs, &depth));

    // single query-string among multiple unrecognized ones
    ASSERT_FAIL(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "TheyWere=25Tons", "EachStoneMyFriend",
                                    "lt=40.5", "ButAmazinglyThey",
                                    "GotThemAllDownInTheSand", NULL),
            &attrs, &depth));

    // invalid query-string value
    ASSERT_FAIL(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "st=What'sTheDealWithStonehenge", NULL),
            &attrs, &depth));

    // unexpected value
    ASSERT_FAIL(PARSE_QUERIES_WRAPPED(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_QUERY,
                                    "YouShouldHaveLeftATinyHint", NULL),
            &attrs, &depth));
}

#undef ASSERT_ATTRIBUTES_EQUAL
#undef ASSERT_ATTRIBUTE_VALUES_EQUAL

static void parse_headers_parse_uri_standard() {
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
}

#ifndef ANJAY_WITH_LWM2M_GATEWAY
AVS_UNIT_TEST(parse_headers, parse_uri) {
    parse_headers_parse_uri_standard();

    bool is_bs;
    anjay_uri_path_t uri;
    header_with_opts_storage_t header_storage;

    // normal, single prefix
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "dev", "10", "11", "12", "13", NULL),
            &is_bs, &uri));

    // "bs" and something more
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "bs", "1", "2", NULL),
            &is_bs, &uri));
}

#else  // ANJAY_WITH_LWM2M_GATEWAY
AVS_UNIT_TEST(parse_headers, parse_uri_with_lwm2m_gateway_support) {
    parse_headers_parse_uri_standard();

    bool is_bs;
    anjay_uri_path_t uri;
    header_with_opts_storage_t header_storage;

    // alphanumeric valid prefix ---------------------------------------------
    // prefix + OID
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "dev1", "1", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    anjay_uri_path_t expected_path = MAKE_OBJECT_PATH_WITH_PREFIX("dev1", 1);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &expected_path));

    // prefix + OID + IID
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "dev1", "1", "2", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    expected_path = MAKE_INSTANCE_PATH_WITH_PREFIX("dev1", 1, 2);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &expected_path));

    // prefix + OID + IID + RID
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "dev1", "1", "2", "3", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    expected_path = MAKE_RESOURCE_PATH_WITH_PREFIX("dev1", 1, 2, 3);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &expected_path));

    // prefix + OID + IID + RID + RIID
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "dev1", "1", "2", "3", "4", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    expected_path = MAKE_RESOURCE_INSTANCE_PATH_WITH_PREFIX("dev1", 1, 2, 3, 4);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &expected_path));

    // invalid prefixes ------------------------------------------------
    // invalid prefix starting with a number
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "1dev", "1", "2", NULL),
            &is_bs, &uri));

    // 2 prefixes
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "dev1", "prefix", "1", "2", NULL),
            &is_bs, &uri));

    // valid prefix + invalid options number --------------------------
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "1dev", "1", "2", "3", "4", "5", NULL),
            &is_bs, &uri));

    // valid prefix max length -----------------------------------
    char prefix[ANJAY_GATEWAY_MAX_PREFIX_LEN];
    for (size_t i = 0; i < sizeof(prefix) - 1; i++) {
        prefix[i] = 'a';
    }
    prefix[sizeof(prefix) - 1] = '\0';
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    prefix, "1", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    expected_path = MAKE_OBJECT_PATH(1);
    strcpy(expected_path.prefix, prefix);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &expected_path));

    // prefix too long -----------------------------------
    char prefix_too_long[ANJAY_GATEWAY_MAX_PREFIX_LEN + 1];
    for (size_t i = 0; i < sizeof(prefix_too_long) - 1; i++) {
        prefix_too_long[i] = 'a';
    }
    prefix_too_long[sizeof(prefix_too_long) - 1] = '\0';
    ASSERT_FAIL(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    prefix_too_long, "1", NULL),
            &is_bs, &uri));

    // reserved "bs" prefix -----------------------------------
    // allowed at this point, but assume it's prefix, not a BS request
    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "bs", "1", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    expected_path = MAKE_OBJECT_PATH_WITH_PREFIX("bs", 1);
    ASSERT_TRUE(_anjay_uri_path_equal(&uri, &expected_path));

    ASSERT_OK(parse_request_uri(
            header_with_string_opts(&header_storage, AVS_COAP_OPTION_URI_PATH,
                                    "bsprefix", "1", NULL),
            &is_bs, &uri));
    ASSERT_FALSE(is_bs);
    ASSERT_TRUE(_anjay_uri_path_prefix_is(&uri, "bsprefix"));
}
#endif // ANJAY_WITH_LWM2M_GATEWAY

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
    anjay_server_connection_t *connection = NULL;
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    connection = _anjay_get_server_connection((const anjay_connection_ref_t) {
        .server = anjay_unlocked->servers,
        .conn_type = ANJAY_CONNECTION_PRIMARY
    });
    ANJAY_MUTEX_UNLOCK(anjay);
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
    // SSID will be read afterwards, twice (second time for attr_storage)
    for (size_t i = 0; i < 2; ++i) {
        if (i == 1) {
            _anjay_mock_dm_expect_list_instances(
                    anjay, &FAKE_SERVER, 0,
                    (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
        }
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
                        { ANJAY_DM_RID_SERVER_NOTIFICATION_STORING,
                          ANJAY_DM_RES_RW, ANJAY_DM_RES_ABSENT },
                        { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW,
                          ANJAY_DM_RES_ABSENT },
                        ANJAY_MOCK_DM_RES_END });
        _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                            ANJAY_DM_RID_SERVER_SSID,
                                            ANJAY_ID_INVALID, 0,
                                            ANJAY_MOCK_DM_INT(0, 1));
    }
    DM_TEST_EXPECT_RESPONSE(mocksocks[0], ACK, CHANGED, ID(0xFA3E), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
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
#ifndef ANJAY_WITHOUT_QUEUE_MODE_AUTOCLOSE
    ASSERT_NULL(connection->queue_mode_close_socket_clb);
#endif // ANJAY_WITHOUT_QUEUE_MODE_AUTOCLOSE

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
#ifdef ANJAY_WITH_LWM2M11
    // attempt to read Preferred Transport
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
#endif // ANJAY_WITH_LWM2M11
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
    anjay_sched_run(anjay);

    const coap_test_msg_t *update_response =
            COAP_MSG(ACK, CHANGED, ID_TOKEN_RAW(0x0000, nth_token(0)),
                     NO_PAYLOAD);
    avs_unit_mocksock_input(mocksocks[0], update_response->content,
                            update_response->length);
    expect_has_buffered_data_check(mocksocks[0], false);
    ASSERT_OK(anjay_serve(anjay, mocksocks[0]));

#ifndef ANJAY_WITHOUT_QUEUE_MODE_AUTOCLOSE
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
#endif // ANJAY_WITHOUT_QUEUE_MODE_AUTOCLOSE

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

static const anjay_mock_dm_res_entry_t FAKE_SECURITY_RESOURCES[] = {
    { ANJAY_DM_RID_SECURITY_SERVER_URI, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
    { ANJAY_DM_RID_SECURITY_MODE, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
    { ANJAY_DM_RID_SECURITY_SSID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
    ANJAY_MOCK_DM_RES_END
};

static const anjay_mock_dm_res_entry_t FAKE_SERVER_RESOURCES[] = {
    { ANJAY_DM_RID_SERVER_SSID, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT },
    { ANJAY_DM_RID_SERVER_LIFETIME, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
    { ANJAY_DM_RID_SERVER_BINDING, ANJAY_DM_RES_RW, ANJAY_DM_RES_PRESENT },
    ANJAY_MOCK_DM_RES_END
};

typedef enum {
    RECONNECT_VERIFY = 0,
    RECONNECT_SUSPENDED,
    RECONNECT_FULL,
    RECONNECT_NONE
} expect_refresh_server_reconnect_mode_t;

typedef struct {
    int dummy;
    expect_refresh_server_reconnect_mode_t with_reconnect;
    anjay_ssid_t ssid;
    size_t server_count;
} expect_refresh_server_additional_args_t;

static void
expect_refresh_server__(anjay_t *anjay,
                        const expect_refresh_server_additional_args_t *args) {
    anjay_ssid_t ssid = args->ssid ? args->ssid : 1;
    size_t server_count = args->server_count ? args->server_count : 1;
    AVS_UNIT_ASSERT_TRUE(ssid <= server_count);

    anjay_iid_t fake_server_instances[server_count + 1];
    for (size_t i = 0; i < server_count; ++i) {
        fake_server_instances[i] = (anjay_iid_t) (i + 1);
    }
    fake_server_instances[server_count] = ANJAY_ID_INVALID;

    _anjay_mock_dm_expect_list_instances(anjay, &FAKE_SERVER, 0,
                                         fake_server_instances);
    // Read SSID
    for (anjay_ssid_t i = 1; i <= ssid; ++i) {
        _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SERVER, i, 0,
                                             FAKE_SERVER_RESOURCES);
        _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, i,
                                            ANJAY_DM_RID_SERVER_SSID,
                                            ANJAY_ID_INVALID, 0,
                                            ANJAY_MOCK_DM_INT(0, i));
    }
    // Read Binding
    _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SERVER, ssid, 0,
                                         FAKE_SERVER_RESOURCES);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, ssid,
                                        ANJAY_DM_RID_SERVER_BINDING,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_STRING(0, "U"));
#ifdef ANJAY_WITH_LWM2M11
    // attempt to read Preferred Transport
    _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SERVER, ssid, 0,
                                         FAKE_SERVER_RESOURCES);
#endif // ANJAY_WITH_LWM2M11
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SECURITY2, 0,
            (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    // attempt to read Bootstrap
    _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SECURITY2, 1, 0,
                                         FAKE_SECURITY_RESOURCES);
    // Read SSID
    _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SECURITY2, 1, 0,
                                         FAKE_SECURITY_RESOURCES);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, ssid));
    // Read Server URI
    _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SECURITY2, 1, 0,
                                         FAKE_SECURITY_RESOURCES);
    _anjay_mock_dm_expect_resource_read(
            anjay, &FAKE_SECURITY2, 1, ANJAY_DM_RID_SECURITY_SERVER_URI,
            ANJAY_ID_INVALID, 0, ANJAY_MOCK_DM_STRING(0, "coap://127.0.0.1"));
    if (args->with_reconnect == RECONNECT_NONE) {
        return;
    }
    if (args->with_reconnect >= RECONNECT_FULL) {
        _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SECURITY2, 1, 0,
                                             FAKE_SECURITY_RESOURCES);
        _anjay_mock_dm_expect_resource_read(
                anjay, &FAKE_SECURITY2, 1, ANJAY_DM_RID_SECURITY_MODE,
                ANJAY_ID_INVALID, 0,
                ANJAY_MOCK_DM_INT(0, ANJAY_SECURITY_NOSEC));
    }
    // Query the data model
    _anjay_mock_dm_expect_list_instances(anjay, &FAKE_SERVER, 0,
                                         fake_server_instances);
    // attempt to read Bootstrap
    _anjay_mock_dm_expect_list_instances(anjay, &FAKE_SERVER, 0,
                                         fake_server_instances);
    // Read SSID
    for (anjay_ssid_t i = 1; i <= ssid; ++i) {
        _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SERVER, i, 0,
                                             FAKE_SERVER_RESOURCES);
        _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, i,
                                            ANJAY_DM_RID_SERVER_SSID,
                                            ANJAY_ID_INVALID, 0,
                                            ANJAY_MOCK_DM_INT(0, i));
    }
    // Read Lifetime
    _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SERVER, ssid, 0,
                                         FAKE_SERVER_RESOURCES);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, ssid,
                                        ANJAY_DM_RID_SERVER_LIFETIME,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 86400));
}

#define expect_refresh_server(...)                                             \
    expect_refresh_server__(AVS_VARARG0(__VA_ARGS__),                          \
                            &(const expect_refresh_server_additional_args_t) { \
                                .dummy = 0 AVS_VARARG_REST(__VA_ARGS__)        \
                            })

typedef struct {
    int dummy;
    anjay_ssid_t ssid;
    size_t server_count;
    uint64_t token_seed;
} force_update_additional_args_t;

static void force_update__(anjay_t *anjay,
                           avs_net_socket_t *mocksock,
                           const force_update_additional_args_t *args) {
    anjay_ssid_t ssid = args->ssid ? args->ssid : 1;
    size_t server_count = args->server_count ? args->server_count : 1;
    AVS_UNIT_ASSERT_TRUE(ssid <= server_count);

    AVS_UNIT_ASSERT_SUCCESS(anjay_schedule_registration_update(anjay, ssid));
    expect_refresh_server(anjay,
                          .ssid = ssid,
                          .server_count = server_count);
    avs_stream_t *payload_memstream = avs_stream_membuf_create();
    AVS_UNIT_ASSERT_NOT_NULL(payload_memstream);
    for (anjay_iid_t i = 1; i <= server_count; ++i) {
        AVS_UNIT_ASSERT_SUCCESS(avs_stream_write_f(
                payload_memstream, "%s</1/%" PRIu16 ">", i > 1 ? "," : "", i));
    }
    void *payload = NULL;
    size_t payload_size = 0;
    AVS_UNIT_ASSERT_SUCCESS(avs_stream_membuf_take_ownership(
            payload_memstream, &payload, &payload_size));
    avs_stream_cleanup(&payload_memstream);
    const coap_test_msg_t *update_request =
            COAP_MSG(CON, POST,
                     ID_TOKEN_RAW(0x0000, nth_token(args->token_seed)),
                     CONTENT_FORMAT(LINK_FORMAT), QUERY("lt=86400", "b=U"),
                     PAYLOAD_EXTERNAL(payload, payload_size));
    avs_unit_mocksock_expect_output(mocksock, update_request->content,
                                    update_request->length);
    anjay_sched_run(anjay);
    avs_free(payload);
    DM_TEST_REQUEST(mocksock, ACK, CHANGED,
                    ID_TOKEN_RAW(0x0000, nth_token(args->token_seed)),
                    NO_PAYLOAD);
    expect_has_buffered_data_check(mocksock, false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksock));
    anjay_sched_run(anjay);
}

#define force_update(Anjay, ...)                               \
    force_update__((Anjay), AVS_VARARG0(__VA_ARGS__),          \
                   &(const force_update_additional_args_t) {   \
                       .dummy = 0 AVS_VARARG_REST(__VA_ARGS__) \
                   })

AVS_UNIT_TEST(reconnect_after_update, test) {
    DM_TEST_INIT_WITH_OBJECTS(&FAKE_SECURITY2, &FAKE_SERVER);
    // Do an initial Update first, to update the registration expire time
    force_update(anjay, mocksocks[0]);

    AVS_UNIT_ASSERT_SUCCESS(anjay_schedule_registration_update(anjay, 1));
    avs_unit_mocksock_expect_shutdown(mocksocks[0]);
    AVS_UNIT_ASSERT_SUCCESS(
            anjay_transport_schedule_reconnect(anjay, ANJAY_TRANSPORT_SET_ALL));

    // ANJAY_SERVER_NEXT_ACTION_SEND_UPDATE
    expect_refresh_server(anjay);
    avs_unit_mocksock_expect_connect(mocksocks[0], "", "");
    avs_unit_mocksock_expect_local_port(mocksocks[0], "5683");
    avs_unit_mocksock_expect_get_opt(mocksocks[0],
                                     AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                                     (avs_net_socket_opt_value_t) {
                                         .flag = true
                                     });
    // reload_servers_sched_job
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SERVER, 0,
            (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SERVER, 1, 0,
                                         FAKE_SERVER_RESOURCES);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID,
                                        ANJAY_ID_INVALID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    _anjay_mock_dm_expect_list_instances(
            anjay, &FAKE_SECURITY2, 0,
            (const anjay_iid_t[]) { 1, ANJAY_ID_INVALID });
    _anjay_mock_dm_expect_list_resources(anjay, &FAKE_SECURITY2, 1, 0,
                                         (const anjay_mock_dm_res_entry_t[]) {
                                                 ANJAY_MOCK_DM_RES_END });

    // retry_or_request_expired_job
    const coap_test_msg_t *update_request =
            COAP_MSG(CON, POST, ID_TOKEN_RAW(0x0001, nth_token(1)), NO_PAYLOAD);
    avs_unit_mocksock_expect_output(mocksocks[0], update_request->content,
                                    update_request->length);
    // ANJAY_SERVER_NEXT_ACTION_REFRESH
    expect_refresh_server(anjay);
    anjay_sched_run(anjay);

    DM_TEST_REQUEST(mocksocks[0], ACK, CHANGED,
                    ID_TOKEN_RAW(0x0001, nth_token(1)), NO_PAYLOAD);
    expect_has_buffered_data_check(mocksocks[0], false);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));
    anjay_sched_run(anjay);

    // Assert that second update is NOT scheduled
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);

    DM_TEST_FINISH;
}

#ifdef ANJAY_WITH_LWM2M11
#    define DM_REGISTER_TEST_CONFIGURATION                                    \
        DM_TEST_CONFIGURATION(.lwm2m_version_config = &(                      \
                                      const anjay_lwm2m_version_config_t) {   \
                                  .minimum_version = ANJAY_LWM2M_VERSION_1_0, \
                                  .maximum_version = ANJAY_LWM2M_VERSION_1_0  \
                              })
#else // ANJAY_WITH_LWM2M11
#    define DM_REGISTER_TEST_CONFIGURATION DM_TEST_CONFIGURATION()
#endif // ANJAY_WITH_LWM2M11

#define DM_REGISTER_TEST_INIT_WITH_SSIDS(...)                                  \
    const anjay_dm_object_def_t *const *obj_defs[] = { &FAKE_SECURITY2,        \
                                                       &FAKE_SERVER };         \
    anjay_ssid_t ssids[] = { __VA_ARGS__ };                                    \
    DM_TEST_INIT_GENERIC(obj_defs, ssids, DM_REGISTER_TEST_CONFIGURATION);     \
    do {                                                                       \
        /* Do initial Updates first to update the registration expire times */ \
        for (size_t _i = 0; _i < AVS_ARRAY_SIZE(ssids); ++_i) {                \
            force_update(anjay, mocksocks[_i],                                 \
                         .ssid = (anjay_ssid_t) (_i + 1),                      \
                         .server_count = AVS_ARRAY_SIZE(ssids),                \
                         .token_seed = _i);                                    \
        }                                                                      \
    } while (false)

static void make_server_inactive(anjay_t *anjay,
                                 anjay_ssid_t ssid,
                                 avs_net_socket_t *mocksock) {
    // Schedule server refresh and make it fail
    ANJAY_MUTEX_LOCK(anjay_unlocked, anjay);
    anjay_server_info_t *server = _anjay_servers_find(anjay_unlocked, ssid);
    AVS_UNIT_ASSERT_NOT_NULL(server);
    _anjay_schedule_refresh_server(server, AVS_TIME_DURATION_ZERO);
    ANJAY_MUTEX_UNLOCK(anjay);
    _anjay_mock_dm_expect_list_instances(anjay, &FAKE_SERVER, -1,
                                         (const anjay_iid_t[]) {
                                                 ANJAY_ID_INVALID });
    avs_unit_mocksock_expect_shutdown(mocksock);
#ifdef ANJAY_WITH_NET_STATS
    avs_unit_mocksock_expect_get_opt(mocksock, AVS_NET_SOCKET_OPT_BYTES_SENT,
                                     (avs_net_socket_opt_value_t) {
                                         .bytes_sent = 0
                                     });
    avs_unit_mocksock_expect_get_opt(mocksock,
                                     AVS_NET_SOCKET_OPT_BYTES_RECEIVED,
                                     (avs_net_socket_opt_value_t) {
                                         .bytes_received = 0
                                     });
#endif // ANJAY_WITH_NET_STATS
#ifdef ANJAY_WITH_LWM2M11
    _anjay_mock_dm_expect_list_instances(anjay, &FAKE_SERVER, -1,
                                         (const anjay_iid_t[]) {
                                                 ANJAY_ID_INVALID });
#endif // ANJAY_WITH_LWM2M11
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
}

AVS_UNIT_TEST(reconnect_server, failures) {
    DM_REGISTER_TEST_INIT_WITH_SSIDS(1);
    // ANJAY_SSID_ANY
    AVS_UNIT_ASSERT_FAILED(
            anjay_server_schedule_reconnect(anjay, ANJAY_SSID_ANY));
    // Nonexistent server
    AVS_UNIT_ASSERT_FAILED(anjay_server_schedule_reconnect(anjay, 42));
    // Inactive server
    make_server_inactive(anjay, 1, mocksocks[0]);
    AVS_UNIT_ASSERT_FAILED(anjay_server_schedule_reconnect(anjay, 1));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(reconnect_server, fresh_session) {
    DM_REGISTER_TEST_INIT_WITH_SSIDS(1);
    avs_unit_mocksock_expect_shutdown(mocksocks[0]);
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_schedule_reconnect(anjay, 1));
    expect_refresh_server(anjay,
                          .with_reconnect = RECONNECT_SUSPENDED);
    avs_unit_mocksock_expect_connect(mocksocks[0], "", "");
    avs_unit_mocksock_expect_local_port(mocksocks[0], "5683");
    avs_unit_mocksock_expect_get_opt(mocksocks[0],
                                     AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                                     (avs_net_socket_opt_value_t) {
                                         .flag = false
                                     });
    const coap_test_msg_t *register_request =
            COAP_MSG(CON, POST, ID_TOKEN_RAW(0x0000, nth_token(1)),
                     CONTENT_FORMAT(LINK_FORMAT), PATH("rd"),
                     QUERY("lwm2m=1.0", "ep=urn:dev:os:anjay-test", "lt=86400"),
                     PAYLOAD("</1/1>"));
    avs_unit_mocksock_expect_output(mocksocks[0], register_request->content,
                                    register_request->length);
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(reconnect_server, resumed_session) {
    DM_REGISTER_TEST_INIT_WITH_SSIDS(1);
    avs_unit_mocksock_expect_shutdown(mocksocks[0]);
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_schedule_reconnect(anjay, 1));
    expect_refresh_server(anjay);
    avs_unit_mocksock_expect_connect(mocksocks[0], "", "");
    avs_unit_mocksock_expect_local_port(mocksocks[0], "5683");
    avs_unit_mocksock_expect_get_opt(mocksocks[0],
                                     AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                                     (avs_net_socket_opt_value_t) {
                                         .flag = true
                                     });
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(schedule_register, nonexistent) {
    DM_REGISTER_TEST_INIT_WITH_SSIDS(1);
    AVS_UNIT_ASSERT_FAILED(anjay_schedule_register(anjay, 42));
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(schedule_register, active_server) {
    DM_REGISTER_TEST_INIT_WITH_SSIDS(1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_schedule_register(anjay, 1));
    expect_refresh_server(anjay);
    const coap_test_msg_t *register_request =
            COAP_MSG(CON, POST, ID_TOKEN_RAW(0x0001, nth_token(1)),
                     CONTENT_FORMAT(LINK_FORMAT), PATH("rd"),
                     QUERY("lwm2m=1.0", "ep=urn:dev:os:anjay-test", "lt=86400"),
                     PAYLOAD("</1/1>"));
    avs_unit_mocksock_expect_output(mocksocks[0], register_request->content,
                                    register_request->length);
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(schedule_register, reconnect_and_register) {
    DM_REGISTER_TEST_INIT_WITH_SSIDS(1);
    avs_unit_mocksock_expect_shutdown(mocksocks[0]);
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_schedule_reconnect(anjay, 1));
    AVS_UNIT_ASSERT_SUCCESS(anjay_schedule_register(anjay, 1));
    expect_refresh_server(anjay);
    avs_unit_mocksock_expect_connect(mocksocks[0], "", "");
    avs_unit_mocksock_expect_local_port(mocksocks[0], "5683");
    avs_unit_mocksock_expect_get_opt(mocksocks[0],
                                     AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                                     (avs_net_socket_opt_value_t) {
                                         .flag = true
                                     });
    const coap_test_msg_t *register_request =
            COAP_MSG(CON, POST, ID_TOKEN_RAW(0x0001, nth_token(1)),
                     CONTENT_FORMAT(LINK_FORMAT), PATH("rd"),
                     QUERY("lwm2m=1.0", "ep=urn:dev:os:anjay-test", "lt=86400"),
                     PAYLOAD("</1/1>"));
    avs_unit_mocksock_expect_output(mocksocks[0], register_request->content,
                                    register_request->length);
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(schedule_register, register_and_reconnect) {
    DM_REGISTER_TEST_INIT_WITH_SSIDS(1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_schedule_register(anjay, 1));
    avs_unit_mocksock_expect_shutdown(mocksocks[0]);
    AVS_UNIT_ASSERT_SUCCESS(anjay_server_schedule_reconnect(anjay, 1));
    expect_refresh_server(anjay);
    avs_unit_mocksock_expect_connect(mocksocks[0], "", "");
    avs_unit_mocksock_expect_local_port(mocksocks[0], "5683");
    avs_unit_mocksock_expect_get_opt(mocksocks[0],
                                     AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                                     (avs_net_socket_opt_value_t) {
                                         .flag = true
                                     });
    const coap_test_msg_t *register_request =
            COAP_MSG(CON, POST, ID_TOKEN_RAW(0x0001, nth_token(1)),
                     CONTENT_FORMAT(LINK_FORMAT), PATH("rd"),
                     QUERY("lwm2m=1.0", "ep=urn:dev:os:anjay-test", "lt=86400"),
                     PAYLOAD("</1/1>"));
    avs_unit_mocksock_expect_output(mocksocks[0], register_request->content,
                                    register_request->length);
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
    DM_TEST_FINISH;
}

static avs_net_socket_t **recreate_mocksock_once_ptr;

static avs_error_t
recreate_udp_mocksock_once(avs_net_socket_t **socket,
                           const avs_net_socket_configuration_t *config) {
    (void) config;
    AVS_UNIT_ASSERT_NULL(*socket);
    *recreate_mocksock_once_ptr = _anjay_test_dm_create_socket(false);
    *socket = *recreate_mocksock_once_ptr;
    AVS_UNIT_MOCK(avs_net_udp_socket_create) = NULL;
    recreate_mocksock_once_ptr = NULL;
    avs_unit_mocksock_expect_connect(*socket, "127.0.0.1", "5683");
    avs_unit_mocksock_expect_local_port(*socket, "5683");
    avs_unit_mocksock_expect_get_opt(*socket,
                                     AVS_NET_SOCKET_OPT_SESSION_RESUMED,
                                     (avs_net_socket_opt_value_t) {
                                         .flag = true
                                     });
    return AVS_OK;
}

AVS_UNIT_TEST(schedule_register,
              inactive_server_doesnt_automatically_reregister) {
    DM_REGISTER_TEST_INIT_WITH_SSIDS(1);
    make_server_inactive(anjay, 1, mocksocks[0]);
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
    AVS_UNIT_ASSERT_SUCCESS(anjay_enable_server(anjay, 1));
    expect_refresh_server(anjay,
                          .with_reconnect = RECONNECT_FULL);
    recreate_mocksock_once_ptr = &mocksocks[0];
    AVS_UNIT_MOCK(avs_net_udp_socket_create) = recreate_udp_mocksock_once;
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_EQUAL(AVS_UNIT_MOCK_INVOCATIONS(avs_net_udp_socket_create),
                          1);
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
    DM_TEST_FINISH;
}

static avs_error_t recreate_udp_mocksock_once_and_expect_register(
        avs_net_socket_t **socket,
        const avs_net_socket_configuration_t *config) {
    AVS_UNIT_ASSERT_SUCCESS(recreate_udp_mocksock_once(socket, config));
    const coap_test_msg_t *register_request =
            COAP_MSG(CON, POST, ID_TOKEN_RAW(0x0000, nth_token(1)),
                     CONTENT_FORMAT(LINK_FORMAT), PATH("rd"),
                     QUERY("lwm2m=1.0", "ep=urn:dev:os:anjay-test", "lt=86400"),
                     PAYLOAD("</1/1>"));
    avs_unit_mocksock_expect_output(*socket, register_request->content,
                                    register_request->length);
    return AVS_OK;
}

AVS_UNIT_TEST(schedule_register, inactive_server) {
    DM_REGISTER_TEST_INIT_WITH_SSIDS(1);
    make_server_inactive(anjay, 1, mocksocks[0]);
    AVS_UNIT_ASSERT_SUCCESS(anjay_schedule_register(anjay, 1));
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
    AVS_UNIT_ASSERT_SUCCESS(anjay_enable_server(anjay, 1));
    expect_refresh_server(anjay,
                          .with_reconnect = RECONNECT_FULL);
    recreate_mocksock_once_ptr = &mocksocks[0];
    AVS_UNIT_MOCK(avs_net_udp_socket_create) =
            recreate_udp_mocksock_once_and_expect_register;
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
    AVS_UNIT_ASSERT_EQUAL(AVS_UNIT_MOCK_INVOCATIONS(avs_net_udp_socket_create),
                          1);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(schedule_register, inactive_server_enable_first) {
    DM_REGISTER_TEST_INIT_WITH_SSIDS(1);
    make_server_inactive(anjay, 1, mocksocks[0]);
    AVS_UNIT_ASSERT_SUCCESS(anjay_enable_server(anjay, 1));
    AVS_UNIT_ASSERT_SUCCESS(anjay_schedule_register(anjay, 1));
    expect_refresh_server(anjay,
                          .with_reconnect = RECONNECT_FULL);
    recreate_mocksock_once_ptr = &mocksocks[0];
    AVS_UNIT_MOCK(avs_net_udp_socket_create) =
            recreate_udp_mocksock_once_and_expect_register;
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
    AVS_UNIT_ASSERT_EQUAL(AVS_UNIT_MOCK_INVOCATIONS(avs_net_udp_socket_create),
                          1);
    DM_TEST_FINISH;
}

AVS_UNIT_TEST(schedule_register, all_servers) {
    DM_REGISTER_TEST_INIT_WITH_SSIDS(1, 2);
    AVS_UNIT_ASSERT_SUCCESS(anjay_schedule_register(anjay, ANJAY_SSID_ANY));
    expect_refresh_server(anjay,
                          .ssid = 1,
                          .server_count = 2);
    expect_refresh_server(anjay,
                          .ssid = 2,
                          .server_count = 2);
    const coap_test_msg_t *register_request1 =
            COAP_MSG(CON, POST, ID_TOKEN_RAW(0x0001, nth_token(2)),
                     CONTENT_FORMAT(LINK_FORMAT), PATH("rd"),
                     QUERY("lwm2m=1.0", "ep=urn:dev:os:anjay-test", "lt=86400"),
                     PAYLOAD("</1/1>,</1/2>"));
    avs_unit_mocksock_expect_output(mocksocks[0], register_request1->content,
                                    register_request1->length);
    const coap_test_msg_t *register_request2 =
            COAP_MSG(CON, POST, ID_TOKEN_RAW(0x0001, nth_token(3)),
                     CONTENT_FORMAT(LINK_FORMAT), PATH("rd"),
                     QUERY("lwm2m=1.0", "ep=urn:dev:os:anjay-test", "lt=86400"),
                     PAYLOAD("</1/1>,</1/2>"));
    avs_unit_mocksock_expect_output(mocksocks[1], register_request2->content,
                                    register_request2->length);
    anjay_sched_run(anjay);
    AVS_UNIT_ASSERT_TRUE(anjay_sched_calculate_wait_time_ms(anjay, INT_MAX)
                         >= 1000);
    DM_TEST_FINISH;
}
