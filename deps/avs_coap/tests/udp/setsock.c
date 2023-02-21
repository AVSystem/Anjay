/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avs_coap_init.h>

#if defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)

#    include <avsystem/coap/coap.h>
#    include <avsystem/commons/avs_errno.h>

#    define MODULE_NAME test
#    include <avs_coap_x_log_config.h>

#    include "./utils.h"

AVS_UNIT_TEST(udp_setsock, callable_only_once) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup(&AVS_COAP_DEFAULT_UDP_TX_PARAMS, 1024, 1024, NULL);
    // socket already set by test_setup()
    ASSERT_FAIL(avs_coap_ctx_set_socket(env.coap_ctx, env.mocksock));
}

AVS_UNIT_TEST(udp_setsock, cleanup_possible_without_socket) {
    test_env_t env __attribute__((cleanup(test_teardown))) =
            test_setup_without_socket(NULL, 1024, 1024, NULL);
    avs_coap_ctx_cleanup(&env.coap_ctx);
}

#endif // defined(AVS_UNIT_TESTING) && defined(WITH_AVS_COAP_UDP)
