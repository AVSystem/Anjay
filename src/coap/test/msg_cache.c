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

#include <alloca.h>

#include <avsystem/commons/unit/test.h>
#include <anjay_modules/utils.h>
#include <anjay_test/mock_clock.h>

#include "../msg_cache.h"
#include "utils.h"

static const anjay_coap_tx_params_t tx_params =
        ANJAY_COAP_DEFAULT_UDP_TX_PARAMS;

static anjay_coap_msg_t *setup_msg_with_id(void *buffer,
                                           uint16_t msg_id,
                                           const char *payload) {
    anjay_coap_msg_t *msg = (anjay_coap_msg_t *) buffer;
    setup_msg(msg, (const uint8_t *) payload, strlen(payload));

    uint16_t net_id = htons(msg_id);
    memcpy(msg->header.message_id, &net_id, sizeof(net_id));

    return msg;
}

AVS_UNIT_TEST(coap_msg_cache, null) {
    static const uint16_t id = 123;
    anjay_coap_msg_t *msg = setup_msg_with_id(alloca(sizeof(*msg)), id, "");

    AVS_UNIT_ASSERT_NULL(_anjay_coap_msg_cache_create(0));
    AVS_UNIT_ASSERT_FAILED(
            _anjay_coap_msg_cache_add(NULL, "host", "port", msg, &tx_params));
    AVS_UNIT_ASSERT_NULL(_anjay_coap_msg_cache_get(NULL, "host", "port", id));

    // these should not crash
    _anjay_coap_msg_cache_release(&(coap_msg_cache_t*){NULL});
    _anjay_coap_msg_cache_debug_print(NULL);
}

AVS_UNIT_TEST(coap_msg_cache, hit_single) {
    coap_msg_cache_t *cache = _anjay_coap_msg_cache_create(1024);

    static const uint16_t id = 123;
    anjay_coap_msg_t *msg = setup_msg_with_id(alloca(sizeof(*msg)), id, "");

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_msg_cache_add(cache, "host", "port", msg, &tx_params));

    // request message existing in cache
    const anjay_coap_msg_t *cached_msg =
            _anjay_coap_msg_cache_get(cache, "host", "port", id);
    AVS_UNIT_ASSERT_NOT_NULL(cached_msg);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, cached_msg,
                                      offsetof(anjay_coap_msg_t, content));

    _anjay_coap_msg_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, hit_multiple) {
    coap_msg_cache_t *cache = _anjay_coap_msg_cache_create(1024);

    static const uint16_t id = 123;
    anjay_coap_msg_t *msg[] = {
        setup_msg_with_id(alloca(sizeof(*msg[0])), (uint16_t)(id + 0), ""),
        setup_msg_with_id(alloca(sizeof(*msg[0])), (uint16_t)(id + 1), ""),
        setup_msg_with_id(alloca(sizeof(*msg[0])), (uint16_t)(id + 2), ""),
    };

    for (size_t i = 0; i < ANJAY_ARRAY_SIZE(msg); ++i) {
        AVS_UNIT_ASSERT_SUCCESS(
                _anjay_coap_msg_cache_add(cache, "host", "port",
                                          msg[i], &tx_params));
    }

    // request message existing in cache
    for (uint16_t i = 0; i < ANJAY_ARRAY_SIZE(msg); ++i) {
        const anjay_coap_msg_t *cached_msg =
                _anjay_coap_msg_cache_get(cache, "host", "port",
                                          (uint16_t)(id + i));
        AVS_UNIT_ASSERT_NOT_NULL(cached_msg);
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg[i], cached_msg,
                                          offsetof(anjay_coap_msg_t, content));
    }

    _anjay_coap_msg_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, hit_expired) {
    coap_msg_cache_t *cache = _anjay_coap_msg_cache_create(1024);

    static const uint16_t id = 123;
    anjay_coap_msg_t *msg = setup_msg_with_id(alloca(sizeof(*msg)), id, "");

    _anjay_mock_clock_start(&(struct timespec){ 100, 0 });

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_msg_cache_add(cache, "host", "port", msg, &tx_params));
    _anjay_mock_clock_advance(&(struct timespec){ 247, 0 });

    // request expired message existing in cache
    AVS_UNIT_ASSERT_NULL(_anjay_coap_msg_cache_get(cache, "host", "port", id));

    _anjay_coap_msg_cache_release(&cache);
    _anjay_mock_clock_finish();
}

AVS_UNIT_TEST(coap_msg_cache, miss_empty) {
    coap_msg_cache_t *cache = _anjay_coap_msg_cache_create(1024);
    static const uint16_t id = 123;

    // request message from empty cache
    AVS_UNIT_ASSERT_NULL(_anjay_coap_msg_cache_get(cache, "host", "port", id));

    _anjay_coap_msg_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, miss_non_empty) {
    coap_msg_cache_t *cache = _anjay_coap_msg_cache_create(1024);

    static const uint16_t id = 123;
    anjay_coap_msg_t *msg = setup_msg_with_id(alloca(sizeof(*msg)), id, "");

    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_msg_cache_add(cache, "host", "port", msg, &tx_params));

    // request message not in cache
    AVS_UNIT_ASSERT_NULL(_anjay_coap_msg_cache_get(cache, "host", "port",
                                                   (uint16_t)(id + 1)));

    _anjay_coap_msg_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, add_existing) {
    coap_msg_cache_t *cache = _anjay_coap_msg_cache_create(1024);

    static const uint16_t id = 123;
    anjay_coap_msg_t *msg = setup_msg_with_id(alloca(sizeof(*msg)), id, "");

    // replacing existing non-expired cached messages with updated ones
    // is not allowed
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_msg_cache_add(cache, "host", "port", msg, &tx_params));
    AVS_UNIT_ASSERT_FAILED(
            _anjay_coap_msg_cache_add(cache, "host", "port", msg, &tx_params));

    _anjay_coap_msg_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, add_existing_expired) {
    coap_msg_cache_t *cache = _anjay_coap_msg_cache_create(1024);

    static const uint16_t id = 123;
    anjay_coap_msg_t *msg = setup_msg_with_id(alloca(sizeof(*msg)), id, "");

    _anjay_mock_clock_start(&(struct timespec){ 100, 0 });

    // replacing existing expired cached messages is not allowed
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_msg_cache_add(cache, "host", "port", msg, &tx_params));
    _anjay_mock_clock_advance(&(struct timespec){ 247, 0 });
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_msg_cache_add(cache, "host", "port", msg, &tx_params));

    _anjay_coap_msg_cache_release(&cache);
    _anjay_mock_clock_finish();
}

AVS_UNIT_TEST(coap_msg_cache, add_evict) {
    static const uint16_t id = 123;
    anjay_coap_msg_t *msg[] = {
        setup_msg_with_id(alloca(sizeof(*msg[0])), (uint16_t)(id + 0), ""),
        setup_msg_with_id(alloca(sizeof(*msg[0])), (uint16_t)(id + 1), ""),
        setup_msg_with_id(alloca(sizeof(*msg[0])), (uint16_t)(id + 2), ""),
    };
    const anjay_coap_msg_t *cached_msg;

    coap_msg_cache_t *cache = _anjay_coap_msg_cache_create(
            (cache_msg_overhead(msg[0]) + offsetof(anjay_coap_msg_t, content))
                    * 2);

    // message with another ID removes oldest existing entry if extra space
    // is required
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_cache_add(cache, "host", "port",
                                                      msg[0], &tx_params));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_cache_add(cache, "host", "port",
                                                      msg[1], &tx_params));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_cache_add(cache, "host", "port",
                                                      msg[2], &tx_params));

    // oldest entry was removed
    AVS_UNIT_ASSERT_NULL(_anjay_coap_msg_cache_get(cache, "host", "port", id));

    // newer entry still exists
    cached_msg = _anjay_coap_msg_cache_get(cache, "host", "port",
                                           (uint16_t)(id + 1));
    AVS_UNIT_ASSERT_NOT_NULL(cached_msg);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg[1], cached_msg,
                                      offsetof(anjay_coap_msg_t, content));

    // newest entry was inserted
    cached_msg = _anjay_coap_msg_cache_get(cache, "host", "port",
                                           (uint16_t)(id + 2));
    AVS_UNIT_ASSERT_NOT_NULL(cached_msg);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg[2], cached_msg,
                                      offsetof(anjay_coap_msg_t, content));

    _anjay_coap_msg_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, add_evict_multiple) {
    static const uint16_t id = 123;
    anjay_coap_msg_t *msg[] = {
        setup_msg_with_id(alloca(sizeof(*msg[0])), (uint16_t)(id + 0), ""),
        setup_msg_with_id(alloca(sizeof(*msg[0])), (uint16_t)(id + 1), ""),
        setup_msg_with_id(alloca(sizeof(*msg[0]) + sizeof("\xFF" "foobarbaz")),
                          (uint16_t)(id + 2), "\xFF" "foobarbaz"),
    };

    coap_msg_cache_t *cache = _anjay_coap_msg_cache_create(
            (cache_msg_overhead(msg[0]) + offsetof(anjay_coap_msg_t, content))
                    * 2);

    // message with another ID removes oldest existing entries if extra space
    // is required
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_cache_add(cache, "host", "port",
                                                      msg[0], &tx_params));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_cache_add(cache, "host", "port",
                                                      msg[1], &tx_params));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_cache_add(cache, "host", "port",
                                                      msg[2], &tx_params));

    // oldest entries were removed
    AVS_UNIT_ASSERT_NULL(_anjay_coap_msg_cache_get(cache, "host", "port", id));
    AVS_UNIT_ASSERT_NULL(_anjay_coap_msg_cache_get(cache, "host", "port",
                                                   (uint16_t)(id + 1)));

    // newest entry was inserted
    const anjay_coap_msg_t *cached_msg =
            _anjay_coap_msg_cache_get(cache, "host", "port",
                                      (uint16_t)(id + 2));
    AVS_UNIT_ASSERT_NOT_NULL(cached_msg);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg[2], cached_msg,
                                      sizeof(*msg[2])
                                      + sizeof("\xFF" "foo") - 1);

    _anjay_coap_msg_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, add_too_big) {
    static const uint16_t id = 123;
    anjay_coap_msg_t *m1 = setup_msg_with_id(alloca(sizeof(*m1)),
                                             (uint16_t)(id + 0), "");
    anjay_coap_msg_t *m2 = setup_msg_with_id(alloca(sizeof(*m2)
                                                    + sizeof("\xFF" "foobarbaz")
                                                    - 1),
                                             (uint16_t)(id + 1),
                                             "\xFF" "foobarbaz");

    coap_msg_cache_t *cache =
            _anjay_coap_msg_cache_create(cache_msg_overhead(m1)
                                         + offsetof(anjay_coap_msg_t, content));

    // message too long to put into cache should be ignored
    AVS_UNIT_ASSERT_SUCCESS(
            _anjay_coap_msg_cache_add(cache, "host", "port", m1, &tx_params));
    AVS_UNIT_ASSERT_FAILED(
            _anjay_coap_msg_cache_add(cache, "host", "port", m2, &tx_params));

    // previously-added entry is still there
    const anjay_coap_msg_t *cached_msg =
            _anjay_coap_msg_cache_get(cache, "host", "port", id);
    AVS_UNIT_ASSERT_NOT_NULL(cached_msg);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(m1, cached_msg,
                                      offsetof(anjay_coap_msg_t, content));

    // "too big" entry was not inserted
    AVS_UNIT_ASSERT_NULL(_anjay_coap_msg_cache_get(cache, "host", "port",
                                                   (uint16_t)(id + 1)));

    _anjay_coap_msg_cache_release(&cache);
}

AVS_UNIT_TEST(coap_msg_cache, multiple_hosts_same_ids) {
    static const uint16_t id = 123;
    anjay_coap_msg_t *m1 = setup_msg_with_id(alloca(sizeof(*m1)), id, "");
    anjay_coap_msg_t *m2 = setup_msg_with_id(alloca(sizeof(*m2)
                                                    + sizeof("\xFF" "foobarbaz")
                                                    - 1),
                                             id, "\xFF" "foobarbaz");

    coap_msg_cache_t *cache = _anjay_coap_msg_cache_create(4096);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_cache_add(cache, "h1", "port",
                                                      m1, &tx_params));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_cache_add(cache, "h2", "port",
                                                      m2, &tx_params));

    // both entries should be present despite having identical IDs
    const anjay_coap_msg_t *cached_msg =
            _anjay_coap_msg_cache_get(cache, "h1", "port", id);
    AVS_UNIT_ASSERT_NOT_NULL(cached_msg);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(m1, cached_msg,
                                      offsetof(anjay_coap_msg_t, content));

    cached_msg = _anjay_coap_msg_cache_get(cache, "h2", "port", id);
    AVS_UNIT_ASSERT_NOT_NULL(cached_msg);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(m2, cached_msg,
                                      offsetof(anjay_coap_msg_t, content)
                                      + sizeof("\xFF" "foobarbaz") - 1);

    _anjay_coap_msg_cache_release(&cache);
}
