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

#ifndef ANJAY_IO_TEST_BIGDATA_H
#define ANJAY_IO_TEST_BIGDATA_H

// clang-format off
#define DATA10B "0123456789"
#define DATA100B \
        DATA10B DATA10B DATA10B DATA10B DATA10B \
        DATA10B DATA10B DATA10B DATA10B DATA10B
#define DATA1kB \
        DATA100B DATA100B DATA100B DATA100B DATA100B \
        DATA100B DATA100B DATA100B DATA100B DATA100B
#define DATA10kB \
        DATA1kB DATA1kB DATA1kB DATA1kB DATA1kB \
        DATA1kB DATA1kB DATA1kB DATA1kB DATA1kB
#define DATA100kB \
        DATA10kB DATA10kB DATA10kB DATA10kB DATA10kB \
        DATA10kB DATA10kB DATA10kB DATA10kB DATA10kB
#define DATA1MB \
        DATA100kB DATA100kB DATA100kB DATA100kB DATA100kB \
        DATA100kB DATA100kB DATA100kB DATA100kB DATA100kB
#define DATA10MB \
        DATA1MB DATA1MB DATA1MB DATA1MB DATA1MB \
        DATA1MB DATA1MB DATA1MB DATA1MB DATA1MB
#define DATA20MB DATA10MB DATA10MB
// clang-format on

#endif /* ANJAY_IO_TEST_BIGDATA_H */
