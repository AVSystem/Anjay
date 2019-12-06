/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include <avs_coap_config.h>

#define MODULE_NAME test
#include <x_log_config.h>

#include <avsystem/coap/coap.h>
#include <avsystem/commons/errno.h>

#include "udp/test/utils.h"

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
