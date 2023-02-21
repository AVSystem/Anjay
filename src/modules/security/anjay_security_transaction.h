/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SECURITY_TRANSACTION_H
#define SECURITY_TRANSACTION_H
#include <anjay_init.h>

#include "anjay_mod_security.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

int _anjay_sec_object_validate_and_process_keys(anjay_unlocked_t *anjay,
                                                sec_repr_t *repr);

int _anjay_sec_transaction_begin_impl(sec_repr_t *repr);
int _anjay_sec_transaction_commit_impl(sec_repr_t *repr);
int _anjay_sec_transaction_validate_impl(anjay_unlocked_t *anjay,
                                         sec_repr_t *repr);
int _anjay_sec_transaction_rollback_impl(sec_repr_t *repr);

VISIBILITY_PRIVATE_HEADER_END

#endif /* SECURITY_TRANSACTION_H */
