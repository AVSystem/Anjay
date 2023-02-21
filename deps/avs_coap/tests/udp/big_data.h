/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef AVS_COAP_SRC_UDP_TEST_BIG_DATA_H
#define AVS_COAP_SRC_UDP_TEST_BIG_DATA_H

#include "./utils.h"

#define DATA_4KB DATA_1KB DATA_1KB DATA_1KB DATA_1KB
#define DATA_16KB DATA_4KB DATA_4KB DATA_4KB DATA_4KB
#define DATA_64KB DATA_16KB DATA_16KB DATA_16KB DATA_16KB
#define DATA_256KB DATA_64KB DATA_64KB DATA_64KB DATA_64KB
#define DATA_1MB DATA_256KB DATA_256KB DATA_256KB DATA_256KB
#define DATA_4MB DATA_1MB DATA_1MB DATA_1MB DATA_1MB
#define DATA_16MB DATA_4MB DATA_4MB DATA_4MB DATA_4MB

#endif // AVS_COAP_SRC_UDP_TEST_BIG_DATA_H
