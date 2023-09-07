#ifndef ANJAY_STANDALONE_SERVER_TRANSACTION_H
#define ANJAY_STANDALONE_SERVER_TRANSACTION_H

#include "standalone_mod_server.h"

int _standalone_serv_object_validate(server_repr_t *repr);

int _standalone_serv_transaction_begin_impl(server_repr_t *repr);
int _standalone_serv_transaction_commit_impl(server_repr_t *repr);
int _standalone_serv_transaction_validate_impl(server_repr_t *repr);
int _standalone_serv_transaction_rollback_impl(server_repr_t *repr);

#endif /* ANJAY_STANDALONE_SERVER_TRANSACTION_H */
