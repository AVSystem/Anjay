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

#include <anjay_init.h>

#include <avsystem/commons/avs_unit_test.h>

#include <avsystem/coap/ctx.h>
#include <avsystem/coap/udp.h>

#include "src/core/anjay_core.h"
#include "src/core/anjay_stats.h"
#include "tests/utils/coap/socket.h"
#include "tests/utils/dm.h"
#include "tests/utils/utils.h"

// HACK to enable _anjay_server_cleanup
#define ANJAY_SERVERS_INTERNALS
#include "src/core/servers/anjay_server_connections.h"
#include "src/core/servers/anjay_servers_internal.h"
#undef ANJAY_SERVERS_INTERNALS

anjay_t *_anjay_test_dm_init(const anjay_configuration_t *config) {
    _anjay_mock_clock_start(avs_time_monotonic_from_scalar(1000, AVS_TIME_S));
    _anjay_mock_dm_expected_commands_clear();
    anjay_t *anjay = anjay_new(config);
    AVS_UNIT_ASSERT_NOT_NULL(anjay);
    _anjay_test_dm_unsched_reload_sockets(anjay);
    return anjay;
}

void _anjay_test_dm_unsched_reload_sockets(anjay_t *anjay) {
    avs_sched_del(&anjay->reload_servers_sched_job_handle);
}

avs_net_socket_t *_anjay_test_dm_install_socket(anjay_t *anjay,
                                                anjay_ssid_t ssid) {
    AVS_UNIT_ASSERT_NOT_NULL(
            AVS_LIST_INSERT_NEW(anjay_server_info_t, &anjay->servers->servers));
    anjay->servers->servers->anjay = anjay;
    anjay->servers->servers->ssid = ssid;
    avs_net_socket_t *socket = NULL;
    _anjay_mocksock_create(&socket, 1252, 1252);
    avs_unit_mocksock_expect_connect(socket, "", "");
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(socket, "", ""));
    anjay->servers->servers->registration_info.expire_time.since_real_epoch
            .seconds = INT64_MAX;
    anjay_server_connection_t *connection =
            _anjay_get_server_connection((const anjay_connection_ref_t) {
                .server = anjay->servers->servers,
                .conn_type = ANJAY_CONNECTION_PRIMARY
            });
    AVS_UNIT_ASSERT_NOT_NULL(connection);
    connection->conn_socket_ = socket;
    connection->coap_ctx = avs_coap_udp_ctx_create(
            anjay->sched, &AVS_COAP_DEFAULT_UDP_TX_PARAMS,
            anjay->in_shared_buffer, anjay->out_shared_buffer,
            anjay->udp_response_cache, anjay->prng_ctx.ctx);
    AVS_UNIT_ASSERT_SUCCESS(
            avs_coap_ctx_set_socket(connection->coap_ctx, socket));

    return socket;
}

void _anjay_test_dm_finish(anjay_t *anjay) {
    anjay_server_info_t *server;
    AVS_LIST_FOREACH(server, anjay->servers->servers) {
        anjay_server_connection_t *connection =
                _anjay_get_server_connection((const anjay_connection_ref_t) {
                    .server = server,
                    .conn_type = ANJAY_CONNECTION_PRIMARY
                });
        if (connection->conn_socket_) {
            avs_unit_mocksock_assert_expects_met(connection->conn_socket_);
            avs_unit_mocksock_assert_io_clean(connection->conn_socket_);
            _anjay_mocksock_expect_stats_zero(connection->conn_socket_);
        }
    }
    _anjay_mock_dm_expect_clean();
    AVS_LIST_CLEAR(&anjay->servers->servers) {
        _anjay_server_cleanup(anjay->servers->servers);
    }
    anjay_delete(anjay);
    _anjay_mock_clock_finish();
}

int _anjay_test_dm_fake_security_list_instances(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_dm_list_ctx_t *ctx) {
    (void) obj_ptr;
    AVS_LIST(anjay_server_info_t) it;
    AVS_LIST_FOREACH(it, anjay->servers->servers) {
        if (it->ssid == ANJAY_ID_INVALID) {
            anjay_dm_emit(ctx, 0);
        } else {
            anjay_dm_emit(ctx, it->ssid);
        }
    }
    return 0;
}

int _anjay_test_dm_fake_security_list_resources(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_dm_resource_list_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;
    anjay_dm_emit_res(ctx,
                      ANJAY_DM_RID_SECURITY_BOOTSTRAP,
                      ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx,
                      ANJAY_DM_RID_SECURITY_SSID,
                      ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    anjay_dm_emit_res(ctx,
                      ANJAY_DM_RID_SECURITY_BOOTSTRAP_TIMEOUT,
                      ANJAY_DM_RES_R,
                      ANJAY_DM_RES_PRESENT);
    return 0;
}

int _anjay_test_dm_fake_security_read(
        anjay_t *anjay,
        const anjay_dm_object_def_t *const *obj_ptr,
        anjay_iid_t iid,
        anjay_rid_t rid,
        anjay_riid_t riid,
        anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    assert(riid == ANJAY_ID_INVALID);
    switch (rid) {
    case ANJAY_DM_RID_SECURITY_BOOTSTRAP:
        return anjay_ret_bool(ctx, (iid == 0));
    case ANJAY_DM_RID_SECURITY_SSID:
        return anjay_ret_i32(ctx, iid ? iid : ANJAY_ID_INVALID);
    case ANJAY_DM_RID_SECURITY_BOOTSTRAP_TIMEOUT:
        return anjay_ret_i32(ctx, 1);
    default:
        return -1;
    }
}
