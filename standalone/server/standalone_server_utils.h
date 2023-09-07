#ifndef ANJAY_STANDALONE_SERVER_UTILS_H
#define ANJAY_STANDALONE_SERVER_UTILS_H

#include <assert.h>

#include "standalone_mod_server.h"

server_repr_t *
_standalone_serv_get(const anjay_dm_object_def_t *const *obj_ptr);

int _standalone_serv_fetch_ssid(anjay_input_ctx_t *ctx, anjay_ssid_t *out_ssid);
int _standalone_serv_fetch_validated_i32(anjay_input_ctx_t *ctx,
                                         int32_t min_value,
                                         int32_t max_value,
                                         int32_t *out_value);
int _standalone_serv_fetch_binding(anjay_input_ctx_t *ctx,
                                   standalone_binding_mode_t *out_binding);

AVS_LIST(server_instance_t)
_standalone_serv_clone_instances(const server_repr_t *repr);
void _standalone_serv_destroy_instances(AVS_LIST(server_instance_t) *instances);
void _standalone_serv_reset_instance(server_instance_t *serv);

#endif /* ANJAY_STANDALONE_SERVER_UTILS_H */
