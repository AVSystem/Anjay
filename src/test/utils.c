/*
 * Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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

#include <stdio.h>

#include <anjay_modules/time_defs.h>

#include <avsystem/commons/unit/test.h>
#include <avsystem/commons/utils.h>

#define ANJAY_URL_EMPTY                           \
    (anjay_url_t) {                         \
        .uri_path = NULL, .uri_query = NULL \
    }

#define AUTO_URL(Name) \
    __attribute__((__cleanup__(_anjay_url_cleanup))) anjay_url_t Name


AVS_UNIT_TEST(parse_url, invalid_protocol_terminator) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http:/acs.avsystem.com",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, protocol_name_too_long) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

#define SIZE (ANJAY_MAX_URL_HOSTNAME_SIZE + 1)
    char url[SIZE] = {
        [0 ... SIZE - 5] = 'A',
        [SIZE - 4] = ':',
        [SIZE - 3] = '/',
        [SIZE - 2] = '/',
        [SIZE - 1] = '\0',
    };
#undef SIZE

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url(url, &parsed_url));
}

AVS_UNIT_TEST(parse_url, square_bracket_enclosed_host_address_too_long) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    char url[ANJAY_MAX_URL_HOSTNAME_SIZE + sizeof("http://[]") + 1] = "http://[";
    memset(url + sizeof("http://[") - 1,
           'A',
           sizeof(url) - sizeof("http://[") - 1);
    url[sizeof(url) - 2] = ']';

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url(url, &parsed_url));
}

AVS_UNIT_TEST(parse_url, without_credentials_port_and_path) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://acs.avsystem.com",
                                             &parsed_url));
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "http");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.host, "acs.avsystem.com");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.port, "");
}

AVS_UNIT_TEST(parse_url, with_port_and_path) {
    AUTO_URL(parsed_url) = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url(
            "http://acs.avsystem.com:123/path/to/resource", &parsed_url));
}

AVS_UNIT_TEST(parse_url, without_password_with_user) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user@acs.avsystem.com:123",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, with_empty_user) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://@acs.avsystem.com:123",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, with_user_and_empty_password) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user:@acs.avsystem.com:123",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, with_empty_user_and_empty_password) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://:@acs.avsystem.com:123",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, with_user_and_password) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user:password@acs.avsystem.com:123",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, escaped_credentials) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user%25:p%40ssword@acs.avsystem.com",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, http_url) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://[12::34]", &parsed_url));
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "http");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.host, "12::34");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.port, "");
}

AVS_UNIT_TEST(parse_url, ftp_url) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    {
        parsed_url = ANJAY_URL_EMPTY;
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("ftp://[12::34]", &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "ftp");
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.host, "12::34");
    }
    {
        parsed_url = ANJAY_URL_EMPTY;
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("ftp://acs.avsystem.com",
                                                 &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "ftp");
    }
    {
        parsed_url = ANJAY_URL_EMPTY;
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("ftp://acs.avsystem.com:123",
                                                 &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "ftp");
    }
}

AVS_UNIT_TEST(parse_url, https_url) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    {
        parsed_url = ANJAY_URL_EMPTY;
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("https://[12::34]",
                                                 &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "https");
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.host, "12::34");
    }
    {
        parsed_url = ANJAY_URL_EMPTY;
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("https://acs.avsystem.com",
                                                 &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "https");
    }
    {
        parsed_url = ANJAY_URL_EMPTY;
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("https://acs.avsystem.com:123",
                                                 &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "https");
    }
}

AVS_UNIT_TEST(parse_url, null_in_username_and_password) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user%00:password@acs.avsystem.com",
                                            &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user:pas%00sword@acs.avsystem.com",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, port_length) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://acs.avsystem.com:1234",
                                             &parsed_url));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://acs.avsystem.com:12345",
                                             &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://acs.avsystem.com:123456",
                                            &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://acs.avsystem.com:1234567",
                                            &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://acs.avsystem.com:",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, port_invalid_characters) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://acs.avsystem.com:12345",
                                             &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://acs.avsystem.com:1_234",
                                            &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://acs.avsystem.com:http",
                                            &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://acs.avsystem.com:12345_",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, ipv6_address) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://[12::34]", &parsed_url));
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "http");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.host, "12::34");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.port, "");
}

AVS_UNIT_TEST(parse_url, ipv6_address_with_port_and_path) {
    AUTO_URL(parsed_url) = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_parse_url("http://[12::34]:56/78", &parsed_url));
}

AVS_UNIT_TEST(parse_url, ipv6_address_with_credentials) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user%25:p%40ssword@[12::34]:56/78",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, invalid_ipv6_address) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://[12:ff:ff::34]",
                                             &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://12:ff:ff::34]",
                                            &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://[12:ff:ff::34",
                                            &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://[12:ff:ff::34]:",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, empty_host) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://host", &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://", &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://:123", &parsed_url));
}

AVS_UNIT_TEST(parse_url, hostname_length) {
    anjay_url_t parsed_url = ANJAY_URL_EMPTY;
    char hostname[ANJAY_MAX_URL_HOSTNAME_SIZE + 1];
    char url[256];

    /* Test max length */
    memset(hostname, 'a', ANJAY_MAX_URL_HOSTNAME_SIZE - 1);
    hostname[ANJAY_MAX_URL_HOSTNAME_SIZE - 1] = '\0';
    sprintf(url, "http://%s", hostname);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url(url, &parsed_url));
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.host, hostname);

    /* Test max length + 1 */
    memset(hostname, 'a', ANJAY_MAX_URL_HOSTNAME_SIZE);
    hostname[ANJAY_MAX_URL_HOSTNAME_SIZE] = '\0';
    sprintf(url, "http://%s", hostname);
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url(url, &parsed_url));
}

AVS_UNIT_TEST(parse_url, empty_uri_path_and_query) {
    anjay_url_t url;
    memset(&url, 0, sizeof(url));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("coaps://avsystem.com/", &url));
    AVS_UNIT_ASSERT_NULL(url.uri_path);
    AVS_UNIT_ASSERT_NULL(url.uri_query);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("coaps://avsystem.com", &url));
    AVS_UNIT_ASSERT_NULL(url.uri_path);
    AVS_UNIT_ASSERT_NULL(url.uri_query);
}

AVS_UNIT_TEST(parse_url, basic_segments) {
    AUTO_URL(url) = ANJAY_URL_EMPTY;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_parse_url("coaps://avsystem.com/0/1/2", &url));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_path), 3);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_query), 0);
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_path, 0)->c_str, "0");
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_path, 1)->c_str, "1");
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_path, 2)->c_str, "2");
}

AVS_UNIT_TEST(parse_url, one_segment_empty) {
    AUTO_URL(url) = ANJAY_URL_EMPTY;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_parse_url("coaps://avsystem.com//", &url));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_path), 1);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_query), 0);
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_path, 0)->c_str, "");
}

AVS_UNIT_TEST(parse_url, two_segments_empty) {
    AUTO_URL(url) = ANJAY_URL_EMPTY;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_parse_url("coaps://avsystem.com///", &url));

    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_path), 2);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_query), 0);
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_path, 0)->c_str, "");
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_path, 1)->c_str, "");
}

AVS_UNIT_TEST(parse_url, basic_query) {
    AUTO_URL(url) = ANJAY_URL_EMPTY;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_parse_url("coaps://avsystem.com/t/o/p?k3k", &url));
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_path), 3);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_query), 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_path, 0)->c_str, "t");
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_path, 1)->c_str, "o");
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_path, 2)->c_str, "p");
    AVS_UNIT_ASSERT_EQUAL_STRING(url.uri_query->c_str, "k3k");
}

AVS_UNIT_TEST(parse_url, basic_query_invalid_chars) {
    AUTO_URL(url) = ANJAY_URL_EMPTY;
    AVS_UNIT_ASSERT_FAILED(
            _anjay_parse_url("coaps://avsystem.com/t/o/p?|<3|<", &url));
    AVS_UNIT_ASSERT_NULL(url.uri_path);
    AVS_UNIT_ASSERT_NULL(url.uri_query);
}

AVS_UNIT_TEST(parse_url, only_query) {
    AUTO_URL(url) = ANJAY_URL_EMPTY;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_parse_url("coaps://avsystem.com/?foo", &url));
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_path), 0);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_query), 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(url.uri_query->c_str, "foo");
}

AVS_UNIT_TEST(parse_url, empty_query_strings) {
    AUTO_URL(url) = ANJAY_URL_EMPTY;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_parse_url("coaps://avsystem.com/?&&&", &url));
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_path), 0);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_query), 4);
    for (size_t i = 0; i < 4; ++i) {
        AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_query, i)->c_str, "");
    }
}

AVS_UNIT_TEST(parse_url, escaped_uri_path) {
    AUTO_URL(url) = ANJAY_URL_EMPTY;
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_parse_url("coap://avsystem.com/foo%26bar", &url));
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_path), 1);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_query), 0);
    AVS_UNIT_ASSERT_EQUAL_STRING(url.uri_path->c_str, "foo&bar");
}

AVS_UNIT_TEST(parse_url, weird_query) {
    AUTO_URL(url) = ANJAY_URL_EMPTY;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url(
            "coap://avsystem.com/foo/bar?baz/weird/but/still/query", &url));
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_path), 2);
    AVS_UNIT_ASSERT_EQUAL(AVS_LIST_SIZE(url.uri_query), 1);
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_path, 0)->c_str, "foo");
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_path, 1)->c_str, "bar");
    AVS_UNIT_ASSERT_EQUAL_STRING(AVS_LIST_NTH(url.uri_query, 0)->c_str,
                                 "baz/weird/but/still/query");
}

AVS_UNIT_TEST(parse_url, bad_percent_encoding) {
    AUTO_URL(url) = ANJAY_URL_EMPTY;
    AVS_UNIT_ASSERT_FAILED(
            _anjay_parse_url("coap://avsystem.com/fo%xa", &url));
    AVS_UNIT_ASSERT_FAILED(
            _anjay_parse_url("coap://avsystem.com/foo?b%xar", &url));
    AVS_UNIT_ASSERT_NULL(url.uri_query);
    AVS_UNIT_ASSERT_NULL(url.uri_path);
}

AVS_UNIT_TEST(snprintf, no_space_for_terminating_nullbyte) {
    char buf[3];
    ssize_t result = avs_simple_snprintf(buf, sizeof(buf), "%s", "foo");
    AVS_UNIT_ASSERT_TRUE(result < 0);
}

AVS_UNIT_TEST(binding_mode_from_str, unsupported_binding_mode) {
    AVS_UNIT_ASSERT_EQUAL(ANJAY_BINDING_NONE,
                          anjay_binding_mode_from_str("☃"));
}
