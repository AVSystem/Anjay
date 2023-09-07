#ifndef ANJAY_STANDALONE_SECURITY_TRANSACTION_H
#define ANJAY_STANDALONE_SECURITY_TRANSACTION_H

#include "standalone_mod_security.h"

int _standalone_sec_object_validate_and_process_keys(anjay_t *anjay,
                                                     sec_repr_t *repr);

int _standalone_sec_transaction_begin_impl(sec_repr_t *repr);
int _standalone_sec_transaction_commit_impl(sec_repr_t *repr);
int _standalone_sec_transaction_validate_impl(anjay_t *anjay, sec_repr_t *repr);
int _standalone_sec_transaction_rollback_impl(sec_repr_t *repr);

#endif /* ANJAY_STANDALONE_SECURITY_TRANSACTION_H */
