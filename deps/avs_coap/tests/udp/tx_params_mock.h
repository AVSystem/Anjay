/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_TEST_TX_PARAMS_MOCK_H
#define AVS_COAP_TEST_TX_PARAMS_MOCK_H

#include <avsystem/commons/avs_unit_mock_helpers.h>

extern __typeof__(_avs_coap_udp_initial_retry_state) *AVS_UNIT_MOCK(
        _avs_coap_udp_initial_retry_state);
void _avs_unit_mock_constructor_avs_coap_udp_initial_retry_state(void)
        __attribute__((constructor));
#define _avs_coap_udp_initial_retry_state(...) \
    AVS_UNIT_MOCK_WRAPPER(_avs_coap_udp_initial_retry_state)(__VA_ARGS__)

#endif /* AVS_COAP_TEST_TX_PARAMS_MOCK_H */
