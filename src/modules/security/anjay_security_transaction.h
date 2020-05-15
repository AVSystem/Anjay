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

#ifndef SECURITY_TRANSACTION_H
#define SECURITY_TRANSACTION_H
#include <anjay_init.h>

#include "anjay_mod_security.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

int _anjay_sec_object_validate(anjay_t *anjay, sec_repr_t *repr);

int _anjay_sec_transaction_begin_impl(sec_repr_t *repr);
int _anjay_sec_transaction_commit_impl(sec_repr_t *repr);
int _anjay_sec_transaction_validate_impl(anjay_t *anjay, sec_repr_t *repr);
int _anjay_sec_transaction_rollback_impl(sec_repr_t *repr);

VISIBILITY_PRIVATE_HEADER_END

#endif /* SECURITY_TRANSACTION_H */
