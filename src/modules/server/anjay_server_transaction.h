/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
