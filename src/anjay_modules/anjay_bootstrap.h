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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_BOOTSTRAP_H
#define ANJAY_INCLUDE_ANJAY_MODULES_BOOTSTRAP_H

#include <stdbool.h>

#include <anjay/io.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef ANJAY_WITH_BOOTSTRAP

bool _anjay_bootstrap_in_progress(anjay_t *anjay);

#else

#    define _anjay_bootstrap_in_progress(anjay) ((void) (anjay), false)

#endif

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_BOOTSTRAP_H */
