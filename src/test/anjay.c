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

#include <config.h>

#include <avsystem/commons/unit/test.h>

#include <errno.h>
#include <stdio.h>

#include <anjay_test/dm.h>

#include "../sched.h"

#include "anjay.h"
#include "mock_coap_stream_impl.h"

AVS_UNIT_GLOBAL_INIT(verbose) {
#ifdef WITH_AVS_LOG
    if (verbose < 2) {
        avs_log_set_default_level(AVS_LOG_QUIET);
    }
#endif
}

#define TEST_NULLABLE_STRING_EQUAL(Actual, Expected) \
    do { \
        if (Expected != NULL) { \
            AVS_UNIT_ASSERT_NOT_NULL((Actual)); \
            AVS_UNIT_ASSERT_EQUAL_STRING((Actual), (Expected)); \
        } else { \
            AVS_UNIT_ASSERT_NULL((Actual)); \
        } \
    } while (0)

#define TEST_SPLIT_QUERY_STRING(QueryString, ExpectedKey, ExpectedValue) \
    do { \
        char buf[] = QueryString; \
        const char *key; \
        const char *value; \
        split_query_string(buf, &key, &value); \
        TEST_NULLABLE_STRING_EQUAL(key, ExpectedKey); \
        TEST_NULLABLE_STRING_EQUAL(value, ExpectedValue); \
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

#define TEST_PARSE_ATTRIBUTE_SUCCESS(Key, Value, ExpectedField, \
                                     ExpectedHasField, ExpectedValue) \
    do { \
        anjay_request_attributes_t attrs; \
        memset(&attrs, 0, sizeof(attrs)); \
        AVS_UNIT_ASSERT_SUCCESS(parse_attribute(&attrs, (Key), (Value))); \
        AVS_UNIT_ASSERT_EQUAL(attrs.values.ExpectedField, (ExpectedValue)); \
        anjay_request_attributes_t expected; \
        memset(&expected, 0, sizeof(expected)); \
        expected.ExpectedHasField = true; \
        expected.values.ExpectedField = (ExpectedValue); \
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &expected, \
                                          sizeof(anjay_request_attributes_t)); \
    } while (0)

#define TEST_PARSE_ATTRIBUTE_IGNORE(Key, Value) \
    do { \
        anjay_request_attributes_t attrs; \
        memset(&attrs, 0, sizeof(attrs)); \
        AVS_UNIT_ASSERT_SUCCESS(parse_attribute(&attrs, (Key), (Value))); \
        anjay_request_attributes_t expected; \
        memset(&expected, 0, sizeof(expected)); \
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &expected, \
                                          sizeof(anjay_request_attributes_t)); \
    } while (0)

#define TEST_PARSE_ATTRIBUTE_FAIL(Key, Value) \
    do { \
        anjay_request_attributes_t attrs; \
        memset(&attrs, 0, sizeof(attrs)); \
        AVS_UNIT_ASSERT_FAILED(parse_attribute(&attrs, (Key), (Value))); \
    } while (0);

AVS_UNIT_TEST(parse_headers, parse_attribute) {
    TEST_PARSE_ATTRIBUTE_SUCCESS("pmin", "123",
                                 min_period, has_min_period, 123);
    TEST_PARSE_ATTRIBUTE_SUCCESS("pmin", NULL,
                                 min_period, has_min_period, -1);
    TEST_PARSE_ATTRIBUTE_FAIL("pmin", "123.4");
    TEST_PARSE_ATTRIBUTE_FAIL("pmin", "woof");
    TEST_PARSE_ATTRIBUTE_FAIL("pmin", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("pmax", "234",
                                 max_period, has_max_period, 234);
    TEST_PARSE_ATTRIBUTE_SUCCESS("pmax", NULL,
                                 max_period, has_max_period, -1);
    TEST_PARSE_ATTRIBUTE_FAIL("pmax", "234.5");
    TEST_PARSE_ATTRIBUTE_FAIL("pmax", "meow");
    TEST_PARSE_ATTRIBUTE_FAIL("pmax", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("gt", "345",
                                 greater_than, has_greater_than, 345.0);
    TEST_PARSE_ATTRIBUTE_SUCCESS("gt", "345.6",
                                 greater_than, has_greater_than, 345.6);
    TEST_PARSE_ATTRIBUTE_SUCCESS("gt", NULL,
                                 greater_than, has_greater_than, NAN);
    TEST_PARSE_ATTRIBUTE_FAIL("gt", "tweet");
    TEST_PARSE_ATTRIBUTE_FAIL("gt", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("lt", "456",
                                 less_than, has_less_than, 456.0);
    TEST_PARSE_ATTRIBUTE_SUCCESS("lt", "456.7",
                                 less_than, has_less_than, 456.7);
    TEST_PARSE_ATTRIBUTE_SUCCESS("lt", NULL, less_than, has_less_than, NAN);
    TEST_PARSE_ATTRIBUTE_FAIL("lt", "squeak");
    TEST_PARSE_ATTRIBUTE_FAIL("lt", "");

    TEST_PARSE_ATTRIBUTE_SUCCESS("st", "567",   step, has_step, 567.0);
    TEST_PARSE_ATTRIBUTE_SUCCESS("st", "567.8", step, has_step, 567.8);
    TEST_PARSE_ATTRIBUTE_SUCCESS("st", NULL,    step, has_step, NAN);
    TEST_PARSE_ATTRIBUTE_FAIL("st", "moo");
    TEST_PARSE_ATTRIBUTE_FAIL("st", "");

    TEST_PARSE_ATTRIBUTE_IGNORE("unknown", "wa-pa-pa-pa-pa-pa-pow");
    TEST_PARSE_ATTRIBUTE_IGNORE("unknown", NULL);
    TEST_PARSE_ATTRIBUTE_IGNORE("unknown", "");
}

#undef TEST_PARSE_ATTRIBUTE_SUCCESS
#undef TEST_PARSE_ATTRIBUTE_FAILED

#define ASSERT_ATTRIBUTE_VALUES_EQUAL(actual, expected) \
    do { \
        AVS_UNIT_ASSERT_EQUAL(actual.min_period, \
                              expected.min_period); \
        AVS_UNIT_ASSERT_EQUAL(actual.max_period, \
                              expected.max_period); \
        ASSERT_DOUBLE_EQUAL(actual.greater_than, expected.greater_than); \
        ASSERT_DOUBLE_EQUAL(actual.less_than, expected.less_than); \
        ASSERT_DOUBLE_EQUAL(actual.step, expected.step); \
    } while (0)

#define ASSERT_ATTRIBUTES_EQUAL(actual, expected) \
    do { \
        AVS_UNIT_ASSERT_EQUAL(actual.has_min_period, expected.has_min_period); \
        AVS_UNIT_ASSERT_EQUAL(actual.has_max_period, expected.has_max_period); \
        AVS_UNIT_ASSERT_EQUAL(actual.has_greater_than, \
                              expected.has_greater_than); \
        AVS_UNIT_ASSERT_EQUAL(actual.has_less_than, expected.has_less_than); \
        AVS_UNIT_ASSERT_EQUAL(actual.has_step, expected.has_step); \
        ASSERT_ATTRIBUTE_VALUES_EQUAL(actual.values, expected.values); \
    } while (0)

AVS_UNIT_TEST(parse_headers, parse_attributes) {
    DECLARE_COAP_STREAM_MOCK(mock);
    avs_stream_abstract_t *stream = (avs_stream_abstract_t*)&mock;

    anjay_request_attributes_t attrs;
    anjay_request_attributes_t empty_attrs;
    memset(&empty_attrs, 0, sizeof(empty_attrs));
    empty_attrs.values = ANJAY_DM_ATTRIBS_EMPTY;
    anjay_request_attributes_t expected_attrs;

    mock.expected_option_number = ANJAY_COAP_OPT_URI_QUERY;

    // no query-strings
    mock.next_opt_value_string = &(const char*[]){ NULL }[0];
    AVS_UNIT_ASSERT_SUCCESS(parse_attributes(stream, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &empty_attrs, sizeof(attrs));

    // single query-string
    mock.next_opt_value_string = &(const char*[]){ "pmin=10", NULL }[0];
    memcpy(&expected_attrs, &empty_attrs, sizeof(expected_attrs));
    expected_attrs.has_min_period = true;
    expected_attrs.values.min_period = 10;
    AVS_UNIT_ASSERT_SUCCESS(parse_attributes(stream, &attrs));
    ASSERT_ATTRIBUTES_EQUAL(attrs, expected_attrs);

    // multiple query-strings
    mock.next_opt_value_string = &(const char*[]){ "pmin=10", "pmax=20", NULL }[0];
    memcpy(&expected_attrs, &empty_attrs, sizeof(expected_attrs));
    expected_attrs.has_min_period = true;
    expected_attrs.values.min_period = 10;
    expected_attrs.has_max_period = true;
    expected_attrs.values.max_period = 20;
    AVS_UNIT_ASSERT_SUCCESS(parse_attributes(stream, &attrs));
    ASSERT_ATTRIBUTES_EQUAL(attrs, expected_attrs);

    // unrecognized query-string only
    mock.next_opt_value_string = &(const char*[]){ "WhatsTheMeaningOf=Stonehenge", NULL }[0];
    AVS_UNIT_ASSERT_SUCCESS(parse_attributes(stream, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &empty_attrs, sizeof(attrs));

    // unrecognized query-string first
    mock.next_opt_value_string = &(const char*[]){ "WhyDidTheyBuildThe=Stonehenge", "pmax=20", NULL }[0];
    memcpy(&expected_attrs, &empty_attrs, sizeof(expected_attrs));
    expected_attrs.has_max_period = true;
    expected_attrs.values.max_period = 20;
    AVS_UNIT_ASSERT_SUCCESS(parse_attributes(stream, &attrs));
    ASSERT_ATTRIBUTES_EQUAL(attrs, expected_attrs);

    // unrecognized query-string last
    mock.next_opt_value_string = &(const char*[]){ "gt=30.5", "AllICanThinkOfIsStonehenge", NULL }[0];
    memcpy(&expected_attrs, &empty_attrs, sizeof(expected_attrs));
    expected_attrs.has_greater_than = true;
    expected_attrs.values.greater_than = 30.5;
    AVS_UNIT_ASSERT_SUCCESS(parse_attributes(stream, &attrs));
    ASSERT_ATTRIBUTES_EQUAL(attrs, expected_attrs);

    // multiple unrecognized query-strings
    mock.next_opt_value_string = &(const char*[]){ "Stonehenge", "Stonehenge", "LotsOfStonesInARow", NULL }[0];
    AVS_UNIT_ASSERT_SUCCESS(parse_attributes(stream, &attrs));
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(&attrs, &empty_attrs, sizeof(attrs));

    // single query-string among multiple unrecognized ones
    mock.next_opt_value_string = &(const char*[]){
        "TheyWere=25Tons", "EachStoneMyFriend",
        "lt=40.5",
        "ButAmazinglyThey", "GotThemAllDownInTheSand",
        NULL
    }[0];
    memcpy(&expected_attrs, &empty_attrs, sizeof(expected_attrs));
    expected_attrs.has_less_than = true;
    expected_attrs.values.less_than = 40.5;
    AVS_UNIT_ASSERT_SUCCESS(parse_attributes(stream, &attrs));
    ASSERT_ATTRIBUTES_EQUAL(attrs, expected_attrs);

    // invalid query-string value
    mock.next_opt_value_string = &(const char*[]){ "st=What'sTheDealWithStonehenge", NULL }[0];
    AVS_UNIT_ASSERT_FAILED(parse_attributes(stream, &attrs));

    // unexpected value
    mock.next_opt_value_string = &(const char*[]){ "pmin=YouShouldHaveLeftATinyHint", NULL }[0];
    AVS_UNIT_ASSERT_FAILED(parse_attributes(stream, &attrs));
}

#undef ASSERT_ATTRIBUTES_EQUAL
#undef ASSERT_ATTRIBUTE_VALUES_EQUAL
#undef ASSERT_DOUBLE_EQUAL

AVS_UNIT_TEST(parse_headers, parse_uri) {
    DECLARE_COAP_STREAM_MOCK(mock);
    avs_stream_abstract_t *stream = (avs_stream_abstract_t*)&mock;

    bool is_bs;
    anjay_oid_t oid;
    anjay_iid_t iid;
    anjay_rid_t rid;
    bool has_oid;
    bool has_iid;
    bool has_rid;

    mock.expected_option_number = ANJAY_COAP_OPT_URI_PATH;

    // OID only
    mock.next_opt_value_string = &(const char*[]){ "1", NULL }[0];
    AVS_UNIT_ASSERT_SUCCESS(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));
    AVS_UNIT_ASSERT_FALSE(is_bs);
    AVS_UNIT_ASSERT_TRUE(has_oid);
    AVS_UNIT_ASSERT_EQUAL(oid, 1);
    AVS_UNIT_ASSERT_FALSE(has_iid);
    AVS_UNIT_ASSERT_FALSE(has_rid);

    // OID+IID
    mock.next_opt_value_string = &(const char*[]){ "2", "3", NULL }[0];
    AVS_UNIT_ASSERT_SUCCESS(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));
    AVS_UNIT_ASSERT_FALSE(is_bs);
    AVS_UNIT_ASSERT_TRUE(has_oid);
    AVS_UNIT_ASSERT_EQUAL(oid, 2);
    AVS_UNIT_ASSERT_TRUE(has_iid);
    AVS_UNIT_ASSERT_EQUAL(iid, 3);
    AVS_UNIT_ASSERT_FALSE(has_rid);

    // OID+IID+RID
    mock.next_opt_value_string = &(const char*[]){ "4", "5", "6", NULL }[0];
    AVS_UNIT_ASSERT_SUCCESS(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));
    AVS_UNIT_ASSERT_FALSE(is_bs);
    AVS_UNIT_ASSERT_TRUE(has_oid);
    AVS_UNIT_ASSERT_EQUAL(oid, 4);
    AVS_UNIT_ASSERT_TRUE(has_iid);
    AVS_UNIT_ASSERT_EQUAL(iid, 5);
    AVS_UNIT_ASSERT_TRUE(has_rid);
    AVS_UNIT_ASSERT_EQUAL(rid, 6);

    // max valid OID/IID/RID
    mock.next_opt_value_string = &(const char*[]){ "65535", "65534", "65535", NULL }[0];
    AVS_UNIT_ASSERT_SUCCESS(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));
    AVS_UNIT_ASSERT_FALSE(is_bs);
    AVS_UNIT_ASSERT_TRUE(has_oid);
    AVS_UNIT_ASSERT_EQUAL(oid, 65535);
    AVS_UNIT_ASSERT_TRUE(has_iid);
    AVS_UNIT_ASSERT_EQUAL(iid, 65534);
    AVS_UNIT_ASSERT_TRUE(has_rid);
    AVS_UNIT_ASSERT_EQUAL(rid, 65535);

    // Bootstrap URI
    mock.next_opt_value_string = &(const char*[]){ "bs", NULL }[0];
    AVS_UNIT_ASSERT_SUCCESS(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));
    AVS_UNIT_ASSERT_TRUE(is_bs);
    AVS_UNIT_ASSERT_FALSE(has_oid);
    AVS_UNIT_ASSERT_FALSE(has_iid);
    AVS_UNIT_ASSERT_FALSE(has_rid);

    // no Request-Uri
    mock.next_opt_value_string = &(const char*[]){ NULL }[0];
    AVS_UNIT_ASSERT_SUCCESS(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));
    AVS_UNIT_ASSERT_FALSE(is_bs);
    AVS_UNIT_ASSERT_FALSE(has_oid);
    AVS_UNIT_ASSERT_FALSE(has_iid);
    AVS_UNIT_ASSERT_FALSE(has_rid);

    // prefix
    mock.next_opt_value_string = &(const char*[]){ "they're taking the hobbits", "to isengard", "7", "8", "9", NULL }[0];
    AVS_UNIT_ASSERT_FAILED(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));

    // prefix that looks like OID + OID+IID+RID
    mock.next_opt_value_string = &(const char*[]){ "100", "10", "11", "12", NULL }[0];
    AVS_UNIT_ASSERT_FAILED(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));

    // prefix that looks like OID/IID/RID + string + OID only
    mock.next_opt_value_string = &(const char*[]){ "100", "101", "102", "wololo", "13", NULL }[0];
    AVS_UNIT_ASSERT_FAILED(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));

    // trailing non-numeric segment
    mock.next_opt_value_string = &(const char*[]){ "14", "NopeChuckTesta", NULL }[0];
    AVS_UNIT_ASSERT_FAILED(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));

    // invalid OID
    mock.next_opt_value_string = &(const char*[]){ "65536", NULL }[0];
    AVS_UNIT_ASSERT_FAILED(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));

    // invalid IID
    mock.next_opt_value_string = &(const char*[]){ "15", "65535", NULL }[0];
    AVS_UNIT_ASSERT_FAILED(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));

    // invalid RID
    mock.next_opt_value_string = &(const char*[]){ "16", "17", "65536", NULL }[0];
    AVS_UNIT_ASSERT_FAILED(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));

    // BS and something more
    mock.next_opt_value_string = &(const char*[]){ "bs", "1", "2", NULL }[0];
    AVS_UNIT_ASSERT_FAILED(parse_request_uri(stream, &is_bs, &has_oid, &oid, &has_iid, &iid, &has_rid, &rid));
}

AVS_UNIT_TEST(parse_headers, parse_action) {
    DECLARE_COAP_STREAM_MOCK(mock);
    avs_stream_abstract_t *stream = (avs_stream_abstract_t*)&mock;

    mock.expected_option_number = ANJAY_COAP_OPT_ACCEPT;

    anjay_request_details_t details;
    memset(&details, 0, sizeof(details));
    details.content_format = ANJAY_COAP_FORMAT_NONE;

    mock.msg_type = ANJAY_COAP_MSG_CONFIRMABLE;
    mock.msg_code = ANJAY_COAP_CODE_GET;
    AVS_UNIT_ASSERT_SUCCESS(parse_action(stream, &details));
    AVS_UNIT_ASSERT_EQUAL(details.request_code, mock.msg_code);
    AVS_UNIT_ASSERT_EQUAL(details.action, ANJAY_ACTION_READ);

    mock.msg_type = ANJAY_COAP_MSG_CONFIRMABLE;
    mock.msg_code = ANJAY_COAP_CODE_GET;
    mock.next_opt_value_uint = ANJAY_COAP_FORMAT_APPLICATION_LINK;
    AVS_UNIT_ASSERT_SUCCESS(parse_action(stream, &details));
    AVS_UNIT_ASSERT_EQUAL(details.request_code, mock.msg_code);
    AVS_UNIT_ASSERT_EQUAL(details.action, ANJAY_ACTION_DISCOVER);

    mock.msg_type = ANJAY_COAP_MSG_CONFIRMABLE;
    mock.msg_code = ANJAY_COAP_CODE_POST;
    details.has_iid = true;
    details.has_rid = true;
    AVS_UNIT_ASSERT_SUCCESS(parse_action(stream, &details));
    AVS_UNIT_ASSERT_EQUAL(details.request_code, mock.msg_code);
    AVS_UNIT_ASSERT_EQUAL(details.action, ANJAY_ACTION_EXECUTE);

    mock.msg_type = ANJAY_COAP_MSG_CONFIRMABLE;
    mock.msg_code = ANJAY_COAP_CODE_POST;
    details.has_iid = false;
    details.has_rid = false;
    details.content_format = ANJAY_COAP_FORMAT_PLAINTEXT;
    AVS_UNIT_ASSERT_SUCCESS(parse_action(stream, &details));
    AVS_UNIT_ASSERT_EQUAL(details.request_code, mock.msg_code);
    AVS_UNIT_ASSERT_EQUAL(details.action, ANJAY_ACTION_CREATE);

    mock.msg_type = ANJAY_COAP_MSG_CONFIRMABLE;
    mock.msg_code = ANJAY_COAP_CODE_POST;
    details.has_iid = true;
    details.has_rid = false;
    details.content_format = ANJAY_COAP_FORMAT_TLV;
    AVS_UNIT_ASSERT_SUCCESS(parse_action(stream, &details));
    AVS_UNIT_ASSERT_EQUAL(details.request_code, mock.msg_code);
    AVS_UNIT_ASSERT_EQUAL(details.action, ANJAY_ACTION_WRITE_UPDATE);

    mock.msg_type = ANJAY_COAP_MSG_CONFIRMABLE;
    mock.msg_code = ANJAY_COAP_CODE_PUT;
    details.content_format = ANJAY_COAP_FORMAT_NONE;
    AVS_UNIT_ASSERT_SUCCESS(parse_action(stream, &details));
    AVS_UNIT_ASSERT_EQUAL(details.request_code, mock.msg_code);
    AVS_UNIT_ASSERT_EQUAL(details.action, ANJAY_ACTION_WRITE_ATTRIBUTES);

    mock.msg_type = ANJAY_COAP_MSG_CONFIRMABLE;
    mock.msg_code = ANJAY_COAP_CODE_PUT;
    details.content_format = ANJAY_COAP_FORMAT_PLAINTEXT;
    AVS_UNIT_ASSERT_SUCCESS(parse_action(stream, &details));
    AVS_UNIT_ASSERT_EQUAL(details.request_code, mock.msg_code);
    AVS_UNIT_ASSERT_EQUAL(details.action, ANJAY_ACTION_WRITE);

    mock.msg_type = ANJAY_COAP_MSG_CONFIRMABLE;
    mock.msg_code = ANJAY_COAP_CODE_DELETE;
    AVS_UNIT_ASSERT_SUCCESS(parse_action(stream, &details));
    AVS_UNIT_ASSERT_EQUAL(details.request_code, mock.msg_code);
    AVS_UNIT_ASSERT_EQUAL(details.action, ANJAY_ACTION_DELETE);

    mock.msg_type = ANJAY_COAP_MSG_CONFIRMABLE;
    mock.msg_code = ANJAY_COAP_CODE_NOT_FOUND;
    AVS_UNIT_ASSERT_FAILED(parse_action(stream, &details));
}

AVS_UNIT_TEST(parse_headers, parse_observe) {
    DECLARE_COAP_STREAM_MOCK(mock);
    avs_stream_abstract_t *stream = (avs_stream_abstract_t*)&mock;

    mock.expected_option_number = ANJAY_COAP_OPT_OBSERVE;

    anjay_coap_observe_t observe;

    mock.next_opt_value_uint = 0;
    AVS_UNIT_ASSERT_SUCCESS(parse_observe(stream, &observe));
    AVS_UNIT_ASSERT_EQUAL(observe, ANJAY_COAP_OBSERVE_REGISTER);

    mock.next_opt_value_uint = 1;
    AVS_UNIT_ASSERT_SUCCESS(parse_observe(stream, &observe));
    AVS_UNIT_ASSERT_EQUAL(observe, ANJAY_COAP_OBSERVE_DEREGISTER);

    mock.next_opt_value_uint = 514;
    AVS_UNIT_ASSERT_FAILED(parse_observe(stream, &observe));

    mock.next_opt_value_uint = -1;
    AVS_UNIT_ASSERT_SUCCESS(parse_observe(stream, &observe));
    AVS_UNIT_ASSERT_EQUAL(observe, ANJAY_COAP_OBSERVE_NONE);
}

static time_t sched_time_to_next_s(anjay_sched_t *sched) {
    struct timespec sched_delay;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_time_to_next(sched, &sched_delay));
    if (sched_delay.tv_nsec >= (500L * 1000L * 1000L)) { // rounding
        return sched_delay.tv_sec + 1;
    } else {
        return sched_delay.tv_sec;
    }
}

AVS_UNIT_TEST(queue_mode, behaviour) {
    static const anjay_dm_attributes_t ATTRS = {
        .min_period = 0,
        .max_period = 9001,
        .greater_than = ANJAY_ATTRIB_VALUE_NONE,
        .less_than = ANJAY_ATTRIB_VALUE_NONE,
        .step = ANJAY_ATTRIB_VALUE_NONE
    };

    ////// INIT //////
    DM_TEST_INIT_WITH_SSIDS(42);
    anjay->servers.active->udp_connection.queue_mode = true;
    static const char REQUEST[] =
            "\x40\x01\xFA\x3E" // CoAP header
            "\x60" // Observe
            "\x52" "42" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0], REQUEST, sizeof(REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_INT(0, 514));
    DM_TEST_EXPECT_READ_NULL_ATTRS(42, 69, 4);
    static const char RESPONSE[] =
            "\x60\x45\xFA\x3E" // CoAP header
            "\x63\xF4\x00\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "514";
    avs_unit_mocksock_expect_output(mocksocks[0],
                                    RESPONSE, sizeof(RESPONSE) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    // observe::flush_send_queue()
    AVS_UNIT_ASSERT_EQUAL(sched_time_to_next_s(anjay->sched), 0);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 42, 0,
                                              &ATTRS);
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 69, 42, 0,
                                                      &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 42, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    AVS_UNIT_ASSERT_EQUAL(sched_time_to_next_s(anjay->sched), 93);
    avs_unit_mocksock_assert_expects_met(mocksocks[0]);

    ////// QUEUE MODE - EMPTY PASS //////
    AVS_UNIT_ASSERT_NOT_NULL(
            anjay->servers.active->udp_connection.queue_mode_suspend_socket_clb_handle);
    _anjay_mock_clock_advance(&(const struct timespec) { 93, 0 });
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    AVS_UNIT_ASSERT_NULL(anjay_get_sockets(anjay));
    AVS_UNIT_ASSERT_NULL(
            anjay->servers.active->udp_connection.queue_mode_suspend_socket_clb_handle);

    ////// NOTIFY - TRIGGER QUEUE MODE //////
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 42, 0,
                                              &ATTRS);
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 69, 42, 0,
                                                      &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 42, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    AVS_UNIT_ASSERT_SUCCESS(anjay_notify_changed(anjay, 42, 69, 4));
    avs_unit_mocksock_assert_expects_met(mocksocks[0]);

    ////// NOTIFICATION //////
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, -1, 0);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read_attrs(anjay, &OBJ, 69, 4, 42, 0,
                                              &ATTRS);
    _anjay_mock_dm_expect_instance_read_default_attrs(anjay, &OBJ, 69, 42, 0,
                                                      &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_object_read_default_attrs(anjay, &OBJ, 42, 0,
                                                    &ANJAY_DM_ATTRIBS_EMPTY);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Hello"));
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, -1, 0);
    static const char NOTIFY_RESPONSE[] =
            "\x50\x45\x69\xED" // CoAP header
            "\x63\x22\x80\x00" // Observe option
            "\x60" // Content-Format
            "\xFF" "Hello";
    avs_unit_mocksock_assert_expects_met(mocksocks[0]);
    avs_unit_mocksock_input_fail(mocksocks[0], -1);
    avs_unit_mocksock_expect_errno(mocksocks[0], ETIMEDOUT);
    avs_unit_mocksock_expect_output(mocksocks[0], NOTIFY_RESPONSE,
                                    sizeof(NOTIFY_RESPONSE) - 1);

    ////// EXECUTE SCHEDULER //////
    while (sched_time_to_next_s(anjay->sched) <= 0) {
        AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    }

    ////// QUEUED RPC //////
    static const char QUEUED_REQUEST[] =
            "\x40\x01\xFB\x3E" // CoAP header
            "\xB2" "42" // OID
            "\x01" "3" // IID
            "\x01" "1"; // RID
    avs_unit_mocksock_input(mocksocks[0],
                            QUEUED_REQUEST, sizeof(QUEUED_REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 3, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 1, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 3, 1, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 3, 1, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Hi!"));
    static const char QUEUED_RESPONSE[] =
            "\x60\x45\xFB\x3E" // CoAP header
            "\xC0" // Content-Format
            "\xFF" "Hi!";
    avs_unit_mocksock_expect_output(mocksocks[0], QUEUED_RESPONSE,
                                    sizeof(QUEUED_RESPONSE) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    ////// NEXT QUEUED RPC - CANCEL NOTIFICATION //////
    static const char QUEUED_REQUEST2[] =
            "\x40\x01\xFC\x3E" // CoAP header
            "\x61\x01" // Observe
            "\x52" "42" // OID
            "\x02" "69" // IID
            "\x01" "4"; // RID
    avs_unit_mocksock_input(mocksocks[0],
                            QUEUED_REQUEST2, sizeof(QUEUED_REQUEST2) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &OBJ, 69, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &OBJ, 4, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &OBJ, 69, 4, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &OBJ, 69, 4, 0,
                                        ANJAY_MOCK_DM_STRING(0, "Meh"));
    static const char QUEUED_RESPONSE2[] =
            "\x60\x45\xFC\x3E" // CoAP header
            "\xC0" // Content-Format
            "\xFF" "Meh";
    avs_unit_mocksock_expect_output(mocksocks[0], QUEUED_RESPONSE2,
                                    sizeof(QUEUED_RESPONSE2) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    ////// EXECUTE SCHEDULER //////
    while (sched_time_to_next_s(anjay->sched) <= 0) {
        AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));
    }

    ////// ASSERT QUEUE MODE //////
    AVS_UNIT_ASSERT_NOT_NULL(
            anjay->servers.active->udp_connection.queue_mode_suspend_socket_clb_handle);
    AVS_UNIT_ASSERT_NOT_NULL(anjay_get_sockets(anjay));
    AVS_UNIT_ASSERT_EQUAL(sched_time_to_next_s(anjay->sched), 93);
    _anjay_mock_clock_advance(&(const struct timespec) { 93, 0 });
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    AVS_UNIT_ASSERT_NULL(anjay_get_sockets(anjay));
    AVS_UNIT_ASSERT_NULL(
            anjay->servers.active->udp_connection.queue_mode_suspend_socket_clb_handle);

    DM_TEST_FINISH;
}

AVS_UNIT_TEST(queue_mode, change) {
    DM_TEST_INIT_WITH_OBJECTS(&OBJ, &FAKE_SECURITY2, &FAKE_SERVER);
    ////// WRITE NEW BINDING //////
    // Write to Binding - dummy data to assert it is actually queried via Read
    static const char WRITE_REQUEST[] =
            "\x40\x03\xFA\x3E" // CoAP header
            "\xB1" "1" // OID
            "\x01" "1" // IID
            "\x01" "7" // RID
            "\x10" // Content-Format
            "\xFF"
            "dummy";
    avs_unit_mocksock_input(mocksocks[0],
                            WRITE_REQUEST, sizeof(WRITE_REQUEST) - 1);
    _anjay_mock_dm_expect_instance_present(anjay, &FAKE_SERVER, 1, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_BINDING, 1);
    _anjay_mock_dm_expect_resource_write(anjay, &FAKE_SERVER, 1,
                                         ANJAY_DM_RID_SERVER_BINDING,
                                         ANJAY_MOCK_DM_STRING(0, "dummy"), 0);
    // SSID will be read afterwards
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    static const char WRITE_RESPONSE[] = "\x60\x44\xFA\x3E"; // 2.04 Changed
    avs_unit_mocksock_expect_output(mocksocks[0],
                                    WRITE_RESPONSE, sizeof(WRITE_RESPONSE) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_serve(anjay, mocksocks[0]));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(anjay_get_sockets(anjay)), 1);
    AVS_UNIT_ASSERT_NULL(
            anjay->servers.active->udp_connection.queue_mode_suspend_socket_clb_handle);

    ////// REFRESH BINDING MODE //////
    // query SSID in Security
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SECURITY2,
                                             ANJAY_DM_RID_SECURITY_BOOTSTRAP, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 1,
                                           ANJAY_DM_RID_SECURITY_BOOTSTRAP, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_BOOTSTRAP, 0,
                                        ANJAY_MOCK_DM_BOOL(0, false));

    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SECURITY2,
                                             ANJAY_DM_RID_SECURITY_SSID, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 1,
                                           ANJAY_DM_RID_SECURITY_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    // query SSID in Server
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    // get Binding
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_BINDING, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_BINDING, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_BINDING, 0,
                                        ANJAY_MOCK_DM_STRING(0, "UQ"));
    // get Security Mode
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SECURITY2,
                                             ANJAY_DM_RID_SECURITY_MODE, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 1,
                                           ANJAY_DM_RID_SECURITY_MODE, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_MODE, 0,
                                        ANJAY_MOCK_DM_INT(
                                                0, ANJAY_UDP_SECURITY_NOSEC));
    // get URI
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SECURITY2,
                                             ANJAY_DM_RID_SECURITY_SERVER_URI,
                                             1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SECURITY2, 1,
                                           ANJAY_DM_RID_SECURITY_SERVER_URI, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SECURITY2, 1,
                                        ANJAY_DM_RID_SECURITY_SERVER_URI, 0,
                                        ANJAY_MOCK_DM_STRING(
                                                0, "coap://127.0.0.1:5683"));
    // data model for the Update message - just fake an empty one
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SECURITY2, 0, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0,
                                      ANJAY_IID_INVALID);
    _anjay_mock_dm_expect_instance_it(anjay, &OBJ, 0, 0, ANJAY_IID_INVALID);
    // lifetime
    _anjay_mock_dm_expect_instance_it(anjay, &FAKE_SERVER, 0, 0, 1);
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_SSID, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_SSID, 0,
                                        ANJAY_MOCK_DM_INT(0, 1));
    _anjay_mock_dm_expect_resource_supported(anjay, &FAKE_SERVER,
                                             ANJAY_DM_RID_SERVER_LIFETIME, 1);
    _anjay_mock_dm_expect_resource_present(anjay, &FAKE_SERVER, 1,
                                           ANJAY_DM_RID_SERVER_LIFETIME, 1);
    _anjay_mock_dm_expect_resource_read(anjay, &FAKE_SERVER, 1,
                                        ANJAY_DM_RID_SERVER_LIFETIME, 0,
                                        ANJAY_MOCK_DM_INT(0, 9001));
    static const char UPDATE[] =
            "\x40\x02\x69\xED"
            "\xC1\x28" // Content-Format
            "\x37" "lt=9001"
            "\x04" "b=UQ"
            "\xFF" "</1>,</42>";
    avs_unit_mocksock_expect_output(mocksocks[0], UPDATE, sizeof(UPDATE) - 1);
    static const char UPDATE_RESPONSE[] = "\x60\x44\x69\xED";
    avs_unit_mocksock_input(mocksocks[0],
                            UPDATE_RESPONSE, sizeof(UPDATE_RESPONSE) - 1);
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    AVS_UNIT_ASSERT_NOT_NULL(
            anjay->servers.active->udp_connection.queue_mode_suspend_socket_clb_handle);
    AVS_UNIT_ASSERT_EQUAL(sched_time_to_next_s(anjay->sched), 93);
    _anjay_mock_clock_advance(&(const struct timespec) { 93, 0 });
    AVS_UNIT_ASSERT_SUCCESS(anjay_sched_run(anjay));

    AVS_UNIT_ASSERT_NULL(anjay_get_sockets(anjay));
    AVS_UNIT_ASSERT_NULL(
            anjay->servers.active->udp_connection.queue_mode_suspend_socket_clb_handle);

    DM_TEST_FINISH;
}
