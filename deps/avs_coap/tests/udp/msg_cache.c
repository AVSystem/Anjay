/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#include <avs_coap_init.h>

#if defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)

#    include <time.h>

#    include <avsystem/commons/avs_defs.h>
#    include <avsystem/commons/avs_memory.h>

#    include <avsystem/coap/code.h>

#    define AVS_UNIT_ENABLE_SHORT_ASSERTS
#    include <avsystem/commons/avs_unit_test.h>

#    include "udp/avs_coap_udp_msg.h"
#    include "udp/avs_coap_udp_msg_cache.h"

#    include "tests/mock_clock.h"

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

static const avs_coap_udp_tx_params_t tx_params = {
    .ack_timeout = { 2, 0 },
    .ack_random_factor = 1.5,
    .max_retransmit = 4
};

typedef struct {
    avs_coap_udp_msg_t udp_msg;
    uint8_t *storage;
    size_t storage_size;
} test_udp_msg_t;

static test_udp_msg_t setup_msg_with_id(uint16_t msg_id, const char *payload) {
    avs_coap_udp_msg_t msg = {
        .header = _avs_coap_udp_header_init(AVS_COAP_UDP_TYPE_ACKNOWLEDGEMENT,
                                            0, AVS_COAP_CODE(3, 4), msg_id),
        .payload = payload,
        .payload_size = strlen(payload)
    };

    size_t size = _avs_coap_udp_msg_size(&msg);
    uint8_t *buf = (uint8_t *) malloc(size);
    size_t written = 0;
    ASSERT_OK(_avs_coap_udp_msg_serialize(&msg, buf, size, &written));
    ASSERT_EQ(size, written);

    return (test_udp_msg_t) {
        .udp_msg = msg,
        .storage = buf,
        .storage_size = size
    };
}

static void free_msg(test_udp_msg_t *msg) {
    free(msg->storage);
}

static void free_msg_array(test_udp_msg_t (*msgs)[3]) {
    for (size_t i = 0; i < 3; ++i) {
        free_msg(&(*msgs)[i]);
    }
}

static void assert_udp_msg_equal(avs_coap_udp_msg_t m1, avs_coap_udp_msg_t m2) {
    ASSERT_EQ(m1.header.code, m2.header.code);
    ASSERT_EQ(_avs_coap_udp_header_get_version(&m1.header),
              _avs_coap_udp_header_get_version(&m2.header));
    ASSERT_EQ(_avs_coap_udp_header_get_token_length(&m1.header),
              _avs_coap_udp_header_get_token_length(&m2.header));
    ASSERT_EQ(_avs_coap_udp_header_get_id(&m1.header),
              _avs_coap_udp_header_get_id(&m2.header));

    ASSERT_EQ(m1.token.size, m2.token.size);
    ASSERT_EQ_BYTES_SIZED(m1.token.bytes, m2.token.bytes, m1.token.size);

    ASSERT_EQ(m1.options.size, m2.options.size);
    ASSERT_EQ_BYTES_SIZED(m1.options.begin, m2.options.begin, m1.options.size);

    ASSERT_EQ(m1.payload_size, m2.payload_size);
    ASSERT_EQ_BYTES_SIZED(m1.payload, m2.payload, m1.payload_size);
}

AVS_UNIT_TEST(coap_msg_cache, null) {
    static const uint16_t id = 123;
    test_udp_msg_t msg __attribute__((cleanup(free_msg))) =
            setup_msg_with_id(id, "");

    ASSERT_NULL(avs_coap_udp_response_cache_create(0));
    ASSERT_FAIL(_avs_coap_udp_response_cache_add(NULL, "host", "port",
                                                 &msg.udp_msg, &tx_params));
    ASSERT_FAIL(_avs_coap_udp_response_cache_get(
            NULL, "host", "port", id, &(avs_coap_udp_cached_response_t) { 0 }));

    // these should not crash
    avs_coap_udp_response_cache_release(
            &(avs_coap_udp_response_cache_t *) { NULL });
}

AVS_UNIT_TEST(coap_msg_cache, hit_single) {
    avs_coap_udp_response_cache_t *cache =
            avs_coap_udp_response_cache_create(1024);

    static const uint16_t id = 123;
    test_udp_msg_t msg __attribute__((cleanup(free_msg))) =
            setup_msg_with_id(id, "");

    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg.udp_msg, &tx_params));

    // request message existing in cache
    avs_coap_udp_cached_response_t cached_msg;
    ASSERT_OK(_avs_coap_udp_response_cache_get(cache, "host", "port", id,
                                               &cached_msg));
    assert_udp_msg_equal(msg.udp_msg, cached_msg.msg);

    avs_coap_udp_response_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, hit_multiple) {
    avs_coap_udp_response_cache_t *cache =
            avs_coap_udp_response_cache_create(1024);

    static const uint16_t id = 123;
    test_udp_msg_t msg[] __attribute__((cleanup(
            free_msg_array))) = { setup_msg_with_id((uint16_t) (id + 0), ""),
                                  setup_msg_with_id((uint16_t) (id + 1), ""),
                                  setup_msg_with_id((uint16_t) (id + 2), "") };

    for (size_t i = 0; i < AVS_ARRAY_SIZE(msg) - 1; ++i) {
        ASSERT_OK(_avs_coap_udp_response_cache_add(
                cache, "host", "port", &msg[i].udp_msg, &tx_params));
    }

    // request message existing in cache
    for (uint16_t i = 0; i < AVS_ARRAY_SIZE(msg) - 1; ++i) {
        avs_coap_udp_cached_response_t cached_msg;
        ASSERT_OK(_avs_coap_udp_response_cache_get(
                cache, "host", "port", (uint16_t) (id + i), &cached_msg));
        assert_udp_msg_equal(msg[i].udp_msg, cached_msg.msg);
    }

    avs_coap_udp_response_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, hit_expired) {
    avs_coap_udp_response_cache_t *cache =
            avs_coap_udp_response_cache_create(1024);

    static const uint16_t id = 123;
    test_udp_msg_t msg __attribute__((cleanup(free_msg))) =
            setup_msg_with_id(id, "");

    _avs_mock_clock_start((avs_time_monotonic_t) { AVS_TIME_DURATION_ZERO });

    AVS_UNIT_ASSERT_SUCCESS(_avs_coap_udp_response_cache_add(
            cache, "host", "port", &msg.udp_msg, &tx_params));
    _avs_mock_clock_advance(avs_time_duration_from_scalar(247, AVS_TIME_S));

    // request expired message existing in cache
    ASSERT_FAIL(_avs_coap_udp_response_cache_get(
            cache, "host", "port", id,
            &(avs_coap_udp_cached_response_t) { 0 }));

    avs_coap_udp_response_cache_release(&cache);

    _avs_mock_clock_finish();
}

AVS_UNIT_TEST(coap_msg_cache, hit_after_expiration) {
    avs_coap_udp_response_cache_t *cache =
            avs_coap_udp_response_cache_create(1024);

    static const uint16_t id1 = 123;
    static const uint16_t id2 = 321;

    test_udp_msg_t msg1 __attribute__((cleanup(free_msg))) =
            setup_msg_with_id(id1, "");
    test_udp_msg_t msg2 __attribute__((cleanup(free_msg))) =
            setup_msg_with_id(id2, "");

    _avs_mock_clock_start((avs_time_monotonic_t) { AVS_TIME_DURATION_ZERO });

    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg1.udp_msg, &tx_params));
    _avs_mock_clock_advance(avs_time_duration_from_scalar(60, AVS_TIME_S));
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg2.udp_msg, &tx_params));
    _avs_mock_clock_advance(avs_time_duration_from_scalar(60, AVS_TIME_S));

    // request expired message existing in cache
    avs_coap_udp_cached_response_t cached_msg;
    ASSERT_OK(_avs_coap_udp_response_cache_get(cache, "host", "port", id2,
                                               &cached_msg));
    assert_udp_msg_equal(msg2.udp_msg, cached_msg.msg);

    avs_coap_udp_response_cache_release(&cache);

    _avs_mock_clock_finish();
}

AVS_UNIT_TEST(coap_msg_cache, miss_empty) {
    avs_coap_udp_response_cache_t *cache =
            avs_coap_udp_response_cache_create(1024);
    static const uint16_t id = 123;

    // request message from empty cache
    ASSERT_FAIL(_avs_coap_udp_response_cache_get(
            cache, "host", "port", id,
            &(avs_coap_udp_cached_response_t) { 0 }));

    avs_coap_udp_response_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, miss_non_empty) {
    avs_coap_udp_response_cache_t *cache =
            avs_coap_udp_response_cache_create(1024);

    static const uint16_t id = 123;
    test_udp_msg_t msg __attribute__((cleanup(free_msg))) =
            setup_msg_with_id(id, "");

    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg.udp_msg, &tx_params));

    // request message not in cache
    ASSERT_FAIL(_avs_coap_udp_response_cache_get(
            cache, "host", "port", (uint16_t) (id + 1),
            &(avs_coap_udp_cached_response_t) { 0 }));

    avs_coap_udp_response_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, add_existing) {
    avs_coap_udp_response_cache_t *cache =
            avs_coap_udp_response_cache_create(1024);

    static const uint16_t id = 123;
    test_udp_msg_t msg __attribute__((cleanup(free_msg))) =
            setup_msg_with_id(id, "");

    // replacing existing non-expired cached messages with updated ones
    // is not allowed
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg.udp_msg, &tx_params));
    ASSERT_FAIL(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                                 &msg.udp_msg, &tx_params));

    avs_coap_udp_response_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, add_existing_expired) {
    avs_coap_udp_response_cache_t *cache =
            avs_coap_udp_response_cache_create(1024);

    static const uint16_t id = 123;
    test_udp_msg_t msg __attribute__((cleanup(free_msg))) =
            setup_msg_with_id(id, "");

    _avs_mock_clock_start((avs_time_monotonic_t) { AVS_TIME_DURATION_ZERO });

    // replacing existing expired cached messages is not allowed
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg.udp_msg, &tx_params));
    _avs_mock_clock_advance(avs_time_duration_from_scalar(247, AVS_TIME_S));
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg.udp_msg, &tx_params));

    avs_coap_udp_response_cache_release(&cache);

    _avs_mock_clock_finish();
}

AVS_UNIT_TEST(coap_msg_cache, add_evict) {
    static const uint16_t id = 123;
    test_udp_msg_t msg[] __attribute__((cleanup(
            free_msg_array))) = { setup_msg_with_id((uint16_t) (id + 0), ""),
                                  setup_msg_with_id((uint16_t) (id + 1), ""),
                                  setup_msg_with_id((uint16_t) (id + 2), "") };
    avs_coap_udp_cached_response_t cached_msg;

    const uint16_t msg_size =
            (uint16_t) _avs_coap_udp_msg_size(&msg[0].udp_msg);
    avs_coap_udp_response_cache_t *cache = avs_coap_udp_response_cache_create(
            (_avs_coap_udp_response_cache_overhead(&msg[0].udp_msg) + msg_size)
            * 2);

    // message with another ID removes oldest existing entry if extra space
    // is required
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg[0].udp_msg, &tx_params));
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg[1].udp_msg, &tx_params));
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg[2].udp_msg, &tx_params));

    // oldest entry was removed
    ASSERT_FAIL(_avs_coap_udp_response_cache_get(
            cache, "host", "port", id,
            &(avs_coap_udp_cached_response_t) { 0 }));

    // newer entry still exists
    ASSERT_OK(_avs_coap_udp_response_cache_get(
            cache, "host", "port", (uint16_t) (id + 1), &cached_msg));
    assert_udp_msg_equal(msg[1].udp_msg, cached_msg.msg);

    // newest entry was inserted
    ASSERT_OK(_avs_coap_udp_response_cache_get(
            cache, "host", "port", (uint16_t) (id + 2), &cached_msg));
    assert_udp_msg_equal(msg[2].udp_msg, cached_msg.msg);

    avs_coap_udp_response_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, add_evict_multiple) {
    static const uint16_t id = 123;
    test_udp_msg_t msg[] __attribute((cleanup(free_msg_array))) = {
        setup_msg_with_id((uint16_t) (id + 0), ""),
        setup_msg_with_id((uint16_t) (id + 1), ""),
        setup_msg_with_id((uint16_t) (id + 2), "\xFF"
                                               "foobarbaz")
    };

    const uint16_t msg_size =
            (uint16_t) _avs_coap_udp_msg_size(&msg[0].udp_msg);
    avs_coap_udp_response_cache_t *cache = avs_coap_udp_response_cache_create(
            (_avs_coap_udp_response_cache_overhead(&msg[0].udp_msg) + msg_size)
            * 2);

    // message with another ID removes oldest existing entries if extra space
    // is required
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg[0].udp_msg, &tx_params));
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg[1].udp_msg, &tx_params));
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &msg[2].udp_msg, &tx_params));

    // oldest entries were removed
    ASSERT_FAIL(_avs_coap_udp_response_cache_get(
            cache, "host", "port", id,
            &(avs_coap_udp_cached_response_t) { 0 }));
    ASSERT_FAIL(_avs_coap_udp_response_cache_get(
            cache, "host", "port", (uint16_t) (id + 1),
            &(avs_coap_udp_cached_response_t) { 0 }));

    // newest entry was inserted
    avs_coap_udp_cached_response_t cached_msg;
    ASSERT_OK(_avs_coap_udp_response_cache_get(
            cache, "host", "port", (uint16_t) (id + 2), &cached_msg));
    assert_udp_msg_equal(msg[2].udp_msg, cached_msg.msg);

    avs_coap_udp_response_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, add_too_big) {
    static const uint16_t id = 123;
    test_udp_msg_t m1 __attribute__((cleanup(free_msg))) =
            setup_msg_with_id((uint16_t) (id + 0), "");
    test_udp_msg_t m2 __attribute__((cleanup(free_msg))) =
            setup_msg_with_id((uint16_t) (id + 1), "\xFF"
                                                   "foobarbaz");

    const uint16_t msg_size = (uint16_t) _avs_coap_udp_msg_size(&m1.udp_msg);
    avs_coap_udp_response_cache_t *cache = avs_coap_udp_response_cache_create(
            _avs_coap_udp_response_cache_overhead(&m1.udp_msg) + msg_size);

    // message too long to put into cache should be ignored
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                               &m1.udp_msg, &tx_params));
    ASSERT_FAIL(_avs_coap_udp_response_cache_add(cache, "host", "port",
                                                 &m2.udp_msg, &tx_params));

    // previously-added entry is still there
    avs_coap_udp_cached_response_t cached_msg;
    ASSERT_OK(_avs_coap_udp_response_cache_get(cache, "host", "port", id,
                                               &cached_msg));
    assert_udp_msg_equal(m1.udp_msg, cached_msg.msg);

    // "too big" entry was not inserted
    ASSERT_FAIL(_avs_coap_udp_response_cache_get(
            cache, "host", "port", (uint16_t) (id + 1),
            &(avs_coap_udp_cached_response_t) { 0 }));

    avs_coap_udp_response_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, multiple_hosts_same_ids) {
    static const uint16_t id = 123;
    test_udp_msg_t m1 __attribute__((cleanup(free_msg))) =
            setup_msg_with_id(id, "");
    test_udp_msg_t m2 __attribute__((cleanup(free_msg))) =
            setup_msg_with_id(id, "\xFF"
                                  "foobarbaz");

    avs_coap_udp_response_cache_t *cache =
            avs_coap_udp_response_cache_create(4096);

    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "h1", "port", &m1.udp_msg,
                                               &tx_params));
    ASSERT_OK(_avs_coap_udp_response_cache_add(cache, "h2", "port", &m2.udp_msg,
                                               &tx_params));

    // both entries should be present despite having identical IDs
    avs_coap_udp_cached_response_t cached_msg;
    ASSERT_OK(_avs_coap_udp_response_cache_get(cache, "h1", "port", id,
                                               &cached_msg));
    assert_udp_msg_equal(m1.udp_msg, cached_msg.msg);

    ASSERT_OK(_avs_coap_udp_response_cache_get(cache, "h2", "port", id,
                                               &cached_msg));
    assert_udp_msg_equal(m2.udp_msg, cached_msg.msg);

    avs_coap_udp_response_cache_release(&cache);
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)
