/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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
