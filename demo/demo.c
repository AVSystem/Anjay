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

#include <sys/time.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <poll.h>

#include <anjay/access_control.h>
#include <anjay/attr_storage.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <unistd.h>

#include "demo.h"
#include "demo_args.h"
#include "demo_cmds.h"
#include "iosched.h"
#include "objects.h"
#include "utils.h"

char **saved_argv;

static int security_object_reload(const anjay_dm_object_def_t *const *sec_obj,
                                  const server_connection_args_t *args) {
    anjay_security_object_purge(sec_obj);
    for (size_t i = 0; i < MAX_SERVERS && args->servers[i].uri; ++i) {
        anjay_security_instance_t instance;
        instance.ssid = ANJAY_SSID_ANY;
        if ((instance.bootstrap_server = args->servers[i].is_bootstrap)) {
            instance.client_holdoff_s = args->bootstrap_holdoff_s;
            instance.bootstrap_timeout_s = args->bootstrap_timeout_s;
        } else {
            instance.client_holdoff_s = -1;
            instance.bootstrap_timeout_s = -1;
            instance.ssid = args->servers[i].id;
        }
        instance.security_mode = args->security_mode;

        /**
         * Note: we can assign pointers by value, as @ref
         * anjay_security_object_add_instance will make a deep copy by itself.
         */
        instance.server_uri = args->servers[i].uri;
        instance.public_cert_or_psk_identity = args->public_cert_or_psk_identity;
        instance.public_cert_or_psk_identity_size = args->public_cert_or_psk_identity_size;
        instance.private_cert_or_psk_key = args->private_cert_or_psk_key;
        instance.private_cert_or_psk_key_size = args->private_cert_or_psk_key_size;
        instance.server_public_key = args->server_public_key;
        instance.server_public_key_size = args->server_public_key_size;

        anjay_iid_t iid = (anjay_iid_t) args->servers[i].id;
        if (anjay_security_object_add_instance(sec_obj, &instance, &iid)) {
            demo_log(ERROR, "Cannot add Security Instance");
            return -1;
        }
    }
    return 0;
}

static int server_object_reload(const anjay_dm_object_def_t *const *serv_obj,
                                const server_connection_args_t *args) {
    anjay_server_object_purge(serv_obj);
    for (size_t i = 0; i < MAX_SERVERS && args->servers[i].uri; ++i) {
        if (args->servers[i].is_bootstrap) {
            continue;
        }

        const anjay_server_instance_t instance = {
            .ssid = args->servers[i].id,
            .lifetime = args->lifetime,
            .default_min_period = -1,
            .default_max_period = -1,
            .disable_timeout = -1,
            .binding = args->binding_mode,
            .notification_storing = true
        };
        anjay_iid_t iid = (anjay_iid_t) args->servers[i].id;
        if (anjay_server_object_add_instance(serv_obj, &instance, &iid)) {
            demo_log(ERROR, "Cannot add Server Instance");
            return -1;
        }
    }
    return 0;
}

void demo_reload_servers(anjay_demo_t *demo) {
    if (security_object_reload(demo->security_obj, demo->connection_args)
        || server_object_reload(demo->server_obj, demo->connection_args)) {
        demo_log(ERROR, "Error while adding new server objects");
        exit(-1);
    }

    int ret_notify_sec = anjay_notify_instances_changed(
            demo->anjay, (*demo->security_obj)->oid);
    int ret_notify_serv = anjay_notify_instances_changed(
            demo->anjay, (*demo->server_obj)->oid);
    if (ret_notify_sec || ret_notify_serv) {
        demo_log(WARNING, "Could not schedule socket reload");
    }
}

static void demo_delete(anjay_demo_t *demo) {
    if (demo->anjay) {
        anjay_delete(demo->anjay);
    }
    apn_conn_profile_object_release(demo->apn_conn_profile_obj);
    cell_connectivity_object_release(demo->cell_connectivity_obj);
    cm_object_release(demo->conn_monitoring_obj);
    cs_object_release(demo->conn_statistics_obj);
    download_diagnostics_object_release(demo->download_diagnostics_obj);
    device_object_release(demo->device_obj);
    ext_dev_info_object_release(demo->ext_dev_info_obj);
    firmware_update_object_release(demo->firmware_update_obj);
    location_object_release(demo->location_obj);
    geopoints_object_release(demo->geopoints_obj);
    ip_ping_object_release(demo->ip_ping_obj);
    test_object_release(demo->test_obj);
    anjay_security_object_delete(demo->security_obj);
    anjay_server_object_delete(demo->server_obj);
    anjay_access_control_object_delete(demo->access_control_obj);

    iosched_release(demo->iosched);
    if (demo->attr_storage) {
        anjay_attr_storage_delete(demo->attr_storage);
    }
    AVS_LIST_CLEAR(&demo->allocated_strings);
    free(demo);
}

static int register_wrapped_object(anjay_demo_t *demo,
                                   const anjay_dm_object_def_t *const *obj) {
    return anjay_register_object(
            demo->anjay,
            anjay_attr_storage_wrap_object(demo->attr_storage, obj));
}

static int add_access_entries(anjay_demo_t *demo,
                              const cmdline_args_t *cmdline_args) {
    const AVS_LIST(access_entry_t) it;
    AVS_LIST_FOREACH(it, cmdline_args->access_entries) {
        if (anjay_access_control_set_acl(demo->access_control_obj,
                                         it->oid, ANJAY_IID_INVALID, it->ssid,
                                         ANJAY_ACCESS_MASK_CREATE)) {
            return -1;
        }
    }
    return 0;
}

static anjay_demo_t *demo_new(cmdline_args_t *cmdline_args) {
    anjay_demo_t *demo = (anjay_demo_t*) calloc(1, sizeof(anjay_demo_t));
    if (!demo) {
        return NULL;
    }

    anjay_configuration_t config = {
        .endpoint_name = cmdline_args->endpoint_name,
        .udp_listen_port = cmdline_args->udp_listen_port,
        .dtls_version = AVS_NET_SSL_VERSION_TLSv1_2,
        .in_buffer_size = (size_t) cmdline_args->inbuf_size,
        .out_buffer_size = (size_t) cmdline_args->outbuf_size
    };

    demo->connection_args = &cmdline_args->connection_args;

    demo->anjay = anjay_new(&config);
    demo->iosched = iosched_create();
    demo->attr_storage = anjay_attr_storage_new(demo->anjay);
    if (!demo->anjay
            || !demo->iosched
            || !demo->attr_storage
            || !iosched_poll_entry_new(demo->iosched, STDIN_FILENO,
                                       POLLIN | POLLHUP,
                                       demo_command_dispatch, demo, NULL)) {
        demo_delete(demo);
        return NULL;
    }

    demo->apn_conn_profile_obj = apn_conn_profile_object_create();
    demo->cell_connectivity_obj =
            cell_connectivity_object_create(demo->apn_conn_profile_obj);
    demo->conn_monitoring_obj = cm_object_create();
    demo->conn_statistics_obj = cs_object_create();
    demo->download_diagnostics_obj = download_diagnostics_object_create(demo->iosched);
    demo->device_obj = device_object_create(demo->iosched,
                                            cmdline_args->endpoint_name);
    demo->ext_dev_info_obj = ext_dev_info_object_create();
    demo->firmware_update_obj =
            firmware_update_object_create(demo->iosched,
                                          cmdline_args->cleanup_fw_on_upgrade);
    demo->server_obj = anjay_server_object_create();
    demo->location_obj = location_object_create();
    demo->geopoints_obj = geopoints_object_create(demo->location_obj);
    demo->ip_ping_obj = ip_ping_object_create(demo->iosched);
    demo->test_obj = test_object_create();
    demo->security_obj = anjay_security_object_create();
    demo->access_control_obj = anjay_access_control_object_new(demo->anjay);

    if (cmdline_args->location_csv
            && location_open_csv(demo->location_obj,
                                 cmdline_args->location_csv,
                                 cmdline_args->location_update_frequency_s)) {
        demo_delete(demo);
        return NULL;
    }

    if (register_wrapped_object(demo, demo->apn_conn_profile_obj)
            || register_wrapped_object(demo, demo->cell_connectivity_obj)
            || register_wrapped_object(demo, demo->conn_monitoring_obj)
            || register_wrapped_object(demo, demo->conn_statistics_obj)
            || register_wrapped_object(demo, demo->download_diagnostics_obj)
            || register_wrapped_object(demo, demo->device_obj)
            || register_wrapped_object(demo, demo->ext_dev_info_obj)
            || register_wrapped_object(demo, demo->firmware_update_obj)
            || register_wrapped_object(demo, demo->security_obj)
            || register_wrapped_object(demo, demo->server_obj)
            || register_wrapped_object(demo, demo->location_obj)
            || register_wrapped_object(demo, demo->geopoints_obj)
            || register_wrapped_object(demo, demo->ip_ping_obj)
            || register_wrapped_object(demo, demo->test_obj)
            || register_wrapped_object(demo, demo->access_control_obj)) {
        demo_delete(demo);
        return NULL;
    }

    demo_reload_servers(demo);

    if (add_access_entries(demo, cmdline_args)) {
        demo_delete(demo);
        return NULL;
    }

    demo->running = true;
    return demo;
}

static void notify_time_dependent(anjay_demo_t *demo) {
    cm_notify_time_dependent(demo->anjay, demo->conn_monitoring_obj);
    ext_dev_info_notify_time_dependent(demo->anjay, demo->ext_dev_info_obj);
    location_notify_time_dependent(demo->anjay, demo->location_obj);
    geopoints_notify_time_dependent(demo->anjay, demo->geopoints_obj);
    test_notify_time_dependent(demo->anjay, demo->test_obj);
}

typedef struct {
    anjay_demo_t *demo;
    avs_net_abstract_socket_t *socket;
    const iosched_entry_t *iosched_entry;
} socket_entry_t;

static void socket_dispatch(short revents, void *arg_) {
    (void) revents;
    socket_entry_t *arg = (socket_entry_t *) arg_;
    int result = anjay_serve(arg->demo->anjay, arg->socket);
    demo_log(DEBUG, "anjay_serve returned %d", result);
}

static socket_entry_t *create_socket_entry(anjay_demo_t *demo,
                                           avs_net_abstract_socket_t *socket) {
    socket_entry_t *entry = AVS_LIST_NEW_ELEMENT(socket_entry_t);
    if (!entry) {
        demo_log(ERROR, "out of memory");
        return NULL;
    }

    const int sys_socket = *(const int*) avs_net_socket_get_system(socket);
    entry->demo = demo;
    entry->socket = socket;
    entry->iosched_entry = iosched_poll_entry_new(demo->iosched, sys_socket,
                                                  POLLIN, socket_dispatch,
                                                  entry, NULL);
    if (!entry->iosched_entry) {
        demo_log(ERROR, "cannot add iosched entry");
        AVS_LIST_DELETE(&entry);
    }
    return entry;
}

static void refresh_socket_entries(anjay_demo_t *demo,
                                   AVS_LIST(socket_entry_t) *entry_ptr) {
    AVS_LIST(avs_net_abstract_socket_t *const) sockets =
            anjay_get_sockets(demo->anjay);

    AVS_LIST(avs_net_abstract_socket_t *const) socket;
    AVS_LIST_FOREACH(socket, sockets) {
        while (*entry_ptr && (*entry_ptr)->socket != *socket) {
            assert((*entry_ptr)->iosched_entry);
            iosched_entry_remove(demo->iosched, (*entry_ptr)->iosched_entry);
            AVS_LIST_DELETE(entry_ptr);
        }
        if (*entry_ptr) {
            assert((*entry_ptr)->iosched_entry);
            entry_ptr = AVS_LIST_NEXT_PTR(entry_ptr);
            continue;
        }
        socket_entry_t *new_entry = create_socket_entry(demo, *socket);
        if (new_entry) {
            AVS_LIST_INSERT(entry_ptr, new_entry);
            entry_ptr = AVS_LIST_NEXT_PTR(entry_ptr);
        }
    }
    while (*entry_ptr) {
        assert((*entry_ptr)->iosched_entry);
        iosched_entry_remove(demo->iosched, (*entry_ptr)->iosched_entry);
        AVS_LIST_DELETE(entry_ptr);
    }
}

static void serve(anjay_demo_t *demo) {
    AVS_LIST(socket_entry_t) socket_entries = NULL;

    struct timespec last_time;
    clock_gettime(CLOCK_REALTIME, &last_time);

    while (demo->running) {
        refresh_socket_entries(demo, &socket_entries);
        demo_log(TRACE, "number of sockets to poll: %u",
                 (unsigned) AVS_LIST_SIZE(socket_entries));

        struct timespec current_time;
        clock_gettime(CLOCK_REALTIME, &current_time);

        if (current_time.tv_sec != last_time.tv_sec) {
            notify_time_dependent(demo);
        }
        last_time = current_time;

        int waitms = anjay_sched_calculate_wait_time_ms(
                demo->anjay,
                (int) ((1000500000 - current_time.tv_nsec) / 1000000));
        demo_log(TRACE, "wait time: %ld.%09ld s",
                 time_to_next_job.tv_sec, time_to_next_job.tv_nsec);

        // +1 to prevent annoying annoying looping in case of
        // sub-millisecond delays
        iosched_run(demo->iosched, waitms + 1);

        if (anjay_sched_run(demo->anjay)) {
            demo->running = false;
        }
    }

    AVS_LIST_CLEAR(&socket_entries) {
        iosched_entry_remove(demo->iosched, socket_entries->iosched_entry);
    }
}

static void log_handler(avs_log_level_t level,
                        const char *module,
                        const char *message) {
    (void)level;
    (void)module;

    char timebuf[128];
    struct timeval now;
    gettimeofday(&now, NULL);

    struct tm *now_tm = localtime(&now.tv_sec);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", now_tm);

    fprintf(stderr, "%s.%06d %s\n",
            timebuf, (int)now.tv_usec, message);
}

int main(int argc, char *argv[]) {
    // 0 ~ stdin, 1 ~ stdout, 2 ~ stderr; close everything else
    for (int fd = 3, maxfd = (int) sysconf(_SC_OPEN_MAX);
            fd < maxfd; ++fd) {
        close(fd);
    }

    avs_log_set_handler(log_handler);
    avs_log_set_default_level(AVS_LOG_TRACE);
    avs_log_set_level(demo, AVS_LOG_DEBUG);
    avs_log_set_level(anjay_sched, AVS_LOG_DEBUG);

    saved_argv = argv;

    cmdline_args_t cmdline_args;
    if (demo_parse_argv(&cmdline_args, argc, argv)) {
        return -1;
    }

    anjay_demo_t *demo = demo_new(&cmdline_args);
    if (!demo) {
        AVS_LIST_CLEAR(&cmdline_args.access_entries);
        return -1;
    }

    serve(demo);
    demo_delete(demo);
    free(cmdline_args.connection_args.public_cert_or_psk_identity);
    free(cmdline_args.connection_args.private_cert_or_psk_key);
    free(cmdline_args.connection_args.server_public_key);
    AVS_LIST_CLEAR(&cmdline_args.access_entries);
    avs_log_reset();
    return 0;
}
