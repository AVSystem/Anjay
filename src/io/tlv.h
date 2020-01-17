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

#ifndef ANJAY_IO_TLV_H
#define ANJAY_IO_TLV_H

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    TLV_ID_IID = 0,
    TLV_ID_RIID = 1,
    TLV_ID_RID_ARRAY = 2,
    TLV_ID_RID = 3
} tlv_id_type_t;

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_IO_TLV_H */
