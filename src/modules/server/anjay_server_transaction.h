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

#ifndef SERVER_TRANSACTION_H
#define SERVER_TRANSACTION_H
#include <anjay_init.h>

#include "anjay_mod_server.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

int _anjay_serv_object_validate(server_repr_t *repr);

int _anjay_serv_transaction_begin_impl(server_repr_t *repr);
int _anjay_serv_transaction_commit_impl(server_repr_t *repr);
int _anjay_serv_transaction_validate_impl(server_repr_t *repr);
int _anjay_serv_transaction_rollback_impl(server_repr_t *repr);

VISIBILITY_PRIVATE_HEADER_END

#endif /* SERVER_TRANSACTION_H */
