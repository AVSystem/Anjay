/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avs_coap_init.h>

#if defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)

#    include "tests/utils.h"

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./helper_functions.h"

AVS_UNIT_TEST(coap_tcp_ctx, create_ctx_and_delete) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();
}

AVS_UNIT_TEST(coap_tcp_ctx, unexpected_response) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();

    const test_msg_t *response = COAP_MSG(CONTENT, MAKE_TOKEN("123"));

    expect_recv(&env, response);
    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

AVS_UNIT_TEST(coap_tcp_ctx, unexpected_response_with_too_big_options) {
    test_env_t env __attribute__((cleanup(test_teardown))) = test_setup();

    // PATH option in response is used only for test purposes.
    const test_msg_t *response =
            COAP_MSG(CONTENT, MAKE_TOKEN("123"),
                     PATH("deszcz na jeziorach deszcz na jeziorach"));

    expect_recv(&env, response);

    avs_coap_borrowed_msg_t request;
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
    ASSERT_OK(receive_nonrequest_message(env.coap_ctx, &request));
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_TCP)
