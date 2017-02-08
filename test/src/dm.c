/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#include <config.h>

#include <avsystem/commons/unit/test.h>

#include <anjay_test/dm.h>
#include <anjay_test/coap/stream.h>

#include "../../src/anjay.h"

// HACK to enable _anjay_server_cleanup
#define ANJAY_SERVERS_INTERNALS
#include "../../src/servers/connection_info.h"
#include "../../src/servers/servers.h"
#undef ANJAY_SERVERS_INTERNALS

anjay_t *_anjay_test_dm_init(void) {
    _anjay_mock_dm_expected_commands_clear();
    anjay_t *anjay = anjay_new(&(anjay_configuration_t) {
                                   .endpoint_name = "urn:dev:os:anjay-test",
                                   .in_buffer_size = 4096,
                                   .out_buffer_size = 4096
                               });
    AVS_UNIT_ASSERT_NOT_NULL(anjay);
    _anjay_mock_coap_stream_setup((coap_stream_t *) anjay->comm_stream);
    _anjay_test_dm_unsched_reload_sockets(anjay);
    return anjay;
}

void _anjay_test_dm_unsched_reload_sockets(anjay_t *anjay) {
    if (anjay->servers.reload_sockets_sched_job_handle) {
        AVS_UNIT_ASSERT_SUCCESS(_anjay_sched_del(
                anjay->sched, &anjay->servers.reload_sockets_sched_job_handle));
    }
}

avs_net_abstract_socket_t *_anjay_test_dm_install_socket(anjay_t *anjay,
                                                         anjay_ssid_t ssid) {
    anjay_active_server_info_t *oldservers = anjay->servers.active;
    AVS_LIST_INSERT_NEW(anjay_active_server_info_t, &anjay->servers.active);
    AVS_UNIT_ASSERT_TRUE(anjay->servers.active != oldservers);
    anjay->servers.active->ssid = ssid;
    avs_net_abstract_socket_t *socket = NULL;
    avs_unit_mocksock_create(&socket);
    avs_unit_mocksock_expect_connect(socket, "", "");
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(socket, "", ""));
    _anjay_connection_internal_set_move_socket(
            &anjay->servers.active->udp_connection, &socket);
    anjay->servers.active->registration_info.expire_time.tv_sec =
            (time_t) ((1UL << (sizeof(time_t) * CHAR_BIT - 1)) - 1UL);
    assert(anjay->servers.active->registration_info.expire_time.tv_sec > 0);
    return _anjay_connection_internal_get_socket(
            &anjay->servers.active->udp_connection);
}

void _anjay_test_dm_finish(anjay_t *anjay) {
    anjay_active_server_info_t *server;
    AVS_LIST_FOREACH(server, anjay->servers.active) {
        avs_net_abstract_socket_t *socket =
                _anjay_connection_internal_get_socket(&server->udp_connection);
        avs_unit_mocksock_assert_expects_met(socket);
        avs_unit_mocksock_assert_io_clean(socket);
    }
    _anjay_mock_dm_expect_clean();
    AVS_LIST_CLEAR(&anjay->servers.active) {
        _anjay_server_cleanup(anjay, anjay->servers.active);
    }
    anjay_delete(anjay);
    _anjay_mock_clock_finish();
}

int _anjay_test_dm_fake_security_instance_it(anjay_t *anjay,
                                             const anjay_dm_object_def_t *const *obj_ptr,
                                             anjay_iid_t *out,
                                             void **cookie_) {
    (void) obj_ptr;
    AVS_LIST(anjay_active_server_info_t) *cookie =
            (AVS_LIST(anjay_active_server_info_t) *) cookie_;
    if (!*cookie) {
        *cookie = anjay->servers.active;
    } else {
        *cookie = AVS_LIST_NEXT(*cookie);
    }
    if (*cookie) {
        *out = ((*cookie)->ssid == ANJAY_IID_INVALID) ? 0 : (*cookie)->ssid;
    } else {
        *out = ANJAY_IID_INVALID;
    }
    return 0;
}

int _anjay_test_dm_fake_security_present(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj_ptr,
                                         anjay_iid_t iid,
                                         anjay_rid_t rid) {
    (void) anjay;
    (void) obj_ptr;
    (void) iid;
    switch (rid) {
    case ANJAY_DM_RID_SECURITY_BOOTSTRAP:
    case ANJAY_DM_RID_SECURITY_SSID:
        return 1;
    default:
        return 0;
    }
}

int _anjay_test_dm_fake_security_read(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj_ptr,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid,
                                      anjay_output_ctx_t *ctx) {
    (void) anjay;
    (void) obj_ptr;
    switch (rid) {
    case ANJAY_DM_RID_SECURITY_BOOTSTRAP:
        return anjay_ret_bool(ctx, (iid == 0));
    case ANJAY_DM_RID_SECURITY_SSID:
        return anjay_ret_i32(ctx, iid ? iid : ANJAY_IID_INVALID);
    default:
        return -1;
    }
}
