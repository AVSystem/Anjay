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

#include <stdio.h>

#include <anjay_modules/time.h>

#include <avsystem/commons/unit/test.h>

AVS_UNIT_TEST(parse_url, without_credentials_port_and_path) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://acs.avsystem.com",
                                             &parsed_url));
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "http");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.host, "acs.avsystem.com");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.port, "");
}

AVS_UNIT_TEST(parse_url, with_path) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://acs.avsystem.com/path",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, with_port_and_path) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://acs.avsystem.com:123/path/to/resource",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, without_password_with_user) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user@acs.avsystem.com:123",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, with_empty_user) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://@acs.avsystem.com:123",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, with_user_and_empty_password) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user:@acs.avsystem.com:123",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, with_empty_user_and_empty_password) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://:@acs.avsystem.com:123",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, with_user_and_password) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user:password@acs.avsystem.com:123",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, escaped_credentials) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user%25:p%40ssword@acs.avsystem.com",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, http_url) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://[12::34]", &parsed_url));
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "http");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.host, "12::34");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.port, "");
}

AVS_UNIT_TEST(parse_url, ftp_url) {
    anjay_url_t parsed_url;

    {
        memset(&parsed_url, 0, sizeof (parsed_url));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("ftp://[12::34]", &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "ftp");
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.host, "12::34");
    }
    {
        memset(&parsed_url, 0, sizeof (parsed_url));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("ftp://acs.avsystem.com",
                                                 &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "ftp");
    }
    {
        memset(&parsed_url, 0, sizeof (parsed_url));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("ftp://acs.avsystem.com:123",
                                                 &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "ftp");
    }
}

AVS_UNIT_TEST(parse_url, https_url) {
    anjay_url_t parsed_url;

    {
        memset(&parsed_url, 0, sizeof (parsed_url));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("https://[12::34]",
                                                 &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "https");
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.host, "12::34");
    }
    {
        memset(&parsed_url, 0, sizeof (parsed_url));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("https://acs.avsystem.com",
                                                 &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "https");
    }
    {
        memset(&parsed_url, 0, sizeof (parsed_url));
        AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("https://acs.avsystem.com:123",
                                                 &parsed_url));
        AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "https");
    }
}

AVS_UNIT_TEST(parse_url, null_in_username_and_password) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user%00:password@acs.avsystem.com",
                                            &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user:pas%00sword@acs.avsystem.com",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, port_length) {
    anjay_url_t parsed_url;

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
    anjay_url_t parsed_url;

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
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://[12::34]", &parsed_url));
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.protocol, "http");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.host, "12::34");
    AVS_UNIT_ASSERT_EQUAL_STRING(parsed_url.port, "");
}

AVS_UNIT_TEST(parse_url, ipv6_address_with_port_and_path) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://[12::34]:56/78",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, ipv6_address_with_credentials) {
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://user%25:p%40ssword@[12::34]:56/78",
                                            &parsed_url));
}

AVS_UNIT_TEST(parse_url, invlaid_ipv6_address) {
    anjay_url_t parsed_url;

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
    anjay_url_t parsed_url;

    AVS_UNIT_ASSERT_SUCCESS(_anjay_parse_url("http://host", &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://", &parsed_url));
    AVS_UNIT_ASSERT_FAILED(_anjay_parse_url("http://:123", &parsed_url));
}

AVS_UNIT_TEST(parse_url, hostname_length) {
    anjay_url_t parsed_url;
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

AVS_UNIT_TEST(time, time_from_ms) {
    struct timespec value;
    _anjay_time_from_ms(&value, 1234);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, 1);
    AVS_UNIT_ASSERT_EQUAL(value.tv_nsec, 234000000L);
    _anjay_time_from_ms(&value, -1234);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, -2);
    AVS_UNIT_ASSERT_EQUAL(value.tv_nsec, 766000000L);
}

AVS_UNIT_TEST(time, add_ms_positive) {
    struct timespec value = { 0, 0 };
    _anjay_time_add_ms(&value, 1);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, 0);
    AVS_UNIT_ASSERT_EQUAL(1 * 1000 * 1000, value.tv_nsec);
}

AVS_UNIT_TEST(time, add_ms_negative) {
    struct timespec value = { 0, 1 * 1000 * 1000 };
    _anjay_time_add_ms(&value, -1);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, 0);
    AVS_UNIT_ASSERT_EQUAL(0, value.tv_nsec);

}

AVS_UNIT_TEST(time, add_ms_positive_overflow) {
    struct timespec value = { 0, 999 * 1000 * 1000 };
    _anjay_time_add_ms(&value, 1);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, 1);
    AVS_UNIT_ASSERT_EQUAL(0, value.tv_nsec);
}

AVS_UNIT_TEST(time, add_ms_negative_underflow) {
    struct timespec value = { 0, 0 };
    _anjay_time_add_ms(&value, -1);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, -1);
    AVS_UNIT_ASSERT_EQUAL(999 * 1000 * 1000, value.tv_nsec);
}

AVS_UNIT_TEST(time, div_ns_only) {
    struct timespec value = { 0, 10 };
    _anjay_time_div(&value, &value, 2);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, 0);
    AVS_UNIT_ASSERT_EQUAL(5, value.tv_nsec);
}

AVS_UNIT_TEST(time, div) {
    struct timespec value = { 1, 10 };
    _anjay_time_div(&value, &value, 2);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, 0);
    AVS_UNIT_ASSERT_EQUAL(value.tv_nsec, 500 * 1000 * 1000 + 5);
}

AVS_UNIT_TEST(time, div_s_rest) {
    struct timespec value = { 3, 500 * 1000 * 1000 };
    _anjay_time_div(&value, &value, 2);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, 1);
    AVS_UNIT_ASSERT_EQUAL(value.tv_nsec, 750 * 1000 * 1000);
}

AVS_UNIT_TEST(time, div_big_divisor) {
    struct timespec value = { 1, 0 };
    _anjay_time_div(&value, &value, 1 * 1000 * 1000 * 1000);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, 0);
    AVS_UNIT_ASSERT_EQUAL(value.tv_nsec, 1);
}

AVS_UNIT_TEST(time, div_big_seconds) {
    struct timespec value = { 999 * 1000 * 1000, 0 };
    _anjay_time_div(&value, &value, 1 * 1000 * 1000 * 1000);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, 0);
    AVS_UNIT_ASSERT_EQUAL(value.tv_nsec, 999 * 1000 * 1000);
}

AVS_UNIT_TEST(time, div_negative) {
    struct timespec value = { -1, 0 };
    _anjay_time_div(&value, &value, 2);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, -1);
    AVS_UNIT_ASSERT_EQUAL(value.tv_nsec, 500 * 1000 * 1000);
}

AVS_UNIT_TEST(time, div_negative_ns) {
    struct timespec value = { -1, 500 * 1000 * 1000 };
    _anjay_time_div(&value, &value, 2);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, -1);
    AVS_UNIT_ASSERT_EQUAL(value.tv_nsec, 750 * 1000 * 1000);
}

AVS_UNIT_TEST(time, div_big_negative) {
    struct timespec value = { -999 * 1000 * 1000, 0 };
    _anjay_time_div(&value, &value, 1 * 1000 * 1000 * 1000);
    AVS_UNIT_ASSERT_EQUAL(value.tv_sec, -1);
    AVS_UNIT_ASSERT_EQUAL(value.tv_nsec, 1 * 1000 * 1000);
}
