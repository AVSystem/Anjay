/*
 * Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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
#include <string.h>
#include <signal.h>

#include <poll.h>
#include <unistd.h>
#include <netinet/in.h>

#include <anjay/access_control.h>
#include <anjay/attr_storage.h>
#include <anjay/fw_update.h>
#include <anjay/security.h>
#include <anjay/server.h>

#include "demo.h"
#include "demo_args.h"
#include "demo_cmds.h"
#include "firmware_update.h"
#include "iosched.h"
#include "objects.h"
#include "demo_utils.h"

char **saved_argv;

static int security_object_reload(const anjay_dm_object_def_t *const *sec_obj,
                                  const server_connection_args_t *args) {
    anjay_security_object_purge(sec_obj);
    const server_entry_t *server;
    DEMO_FOREACH_SERVER_ENTRY(server, args) {
        anjay_security_instance_t instance;
        memset(&instance, 0, sizeof(instance));
        instance.ssid = ANJAY_SSID_ANY;
        if ((instance.bootstrap_server = server->is_bootstrap)) {
            instance.client_holdoff_s = args->bootstrap_holdoff_s;
            instance.bootstrap_timeout_s = args->bootstrap_timeout_s;
        } else {
            instance.client_holdoff_s = -1;
            instance.bootstrap_timeout_s = -1;
            instance.ssid = server->id;
        }
        instance.security_mode = args->security_mode;

        /**
         * Note: we can assign pointers by value, as @ref
         * anjay_security_object_add_instance will make a deep copy by itself.
         */
        instance.server_uri = server->uri;
        instance.public_cert_or_psk_identity = args->public_cert_or_psk_identity;
        instance.public_cert_or_psk_identity_size = args->public_cert_or_psk_identity_size;
        instance.private_cert_or_psk_key = args->private_cert_or_psk_key;
        instance.private_cert_or_psk_key_size = args->private_cert_or_psk_key_size;
        instance.server_public_key = args->server_public_key;
        instance.server_public_key_size = args->server_public_key_size;

        anjay_iid_t iid = server->security_iid;
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
    const server_entry_t *server;
    DEMO_FOREACH_SERVER_ENTRY(server, args) {
        if (server->is_bootstrap) {
            continue;
        }

        const anjay_server_instance_t instance = {
            .ssid = server->id,
            .lifetime = args->lifetime,
            .default_min_period = -1,
            .default_max_period = -1,
            .disable_timeout = -1,
            .binding = args->binding_mode,
            .notification_storing = true
        };
        anjay_iid_t iid = server->server_iid;
        if (anjay_server_object_add_instance(serv_obj, &instance, &iid)) {
            demo_log(ERROR, "Cannot add Server Instance");
            return -1;
        }
    }
    return 0;
}

const anjay_dm_object_def_t **demo_find_object(anjay_demo_t *demo,
                                               anjay_oid_t oid) {
    AVS_LIST(anjay_demo_object_t) object;
    AVS_LIST_FOREACH(object, demo->objects) {
        if ((*object->obj_ptr)->oid == oid) {
            return object->obj_ptr;
        }
    }
    return NULL;
}

void demo_reload_servers(anjay_demo_t *demo) {
    const anjay_dm_object_def_t **security_obj =
            demo_find_object(demo, DEMO_OID_SECURITY);
    const anjay_dm_object_def_t **server_obj =
            demo_find_object(demo, DEMO_OID_SERVER);

    if (!security_obj || !server_obj) {
        demo_log(ERROR, "Either security or server object is not registered");
        exit(-1);
    }

    if (security_object_reload(security_obj, demo->connection_args)
        || server_object_reload(server_obj, demo->connection_args)) {
        demo_log(ERROR, "Error while adding new server objects");
        exit(-1);
    }

    int ret_notify_sec = anjay_notify_instances_changed(demo->anjay,
                                                        (*security_obj)->oid);
    int ret_notify_serv = anjay_notify_instances_changed(demo->anjay,
                                                         (*server_obj)->oid);
    if (ret_notify_sec || ret_notify_serv) {
        demo_log(WARNING, "Could not schedule socket reload");
    }
}

static void demo_delete(anjay_demo_t *demo) {
    if (demo->anjay) {
        anjay_delete(demo->anjay);
    }
    AVS_LIST_CLEAR(&demo->objects) {
        demo->objects->release_func(demo->objects->obj_ptr);
    }
    firmware_update_destroy(&demo->fw_update);

    iosched_release(demo->iosched);
    AVS_LIST_CLEAR(&demo->allocated_strings);
    free(demo);
}

static bool has_bootstrap_server(anjay_demo_t *demo) {
    const server_entry_t *server;
    DEMO_FOREACH_SERVER_ENTRY(server, demo->connection_args) {
        if (server->is_bootstrap) {
            return true;
        }
    }
    return false;
}

static size_t count_non_bootstrap_servers(anjay_demo_t *demo) {
    size_t result = 0;
    const server_entry_t *server;
    DEMO_FOREACH_SERVER_ENTRY(server, demo->connection_args) {
        if (!server->is_bootstrap) {
            ++result;
        }
    }
    return result;
}

static int add_default_access_entries(anjay_demo_t *demo) {
    if (has_bootstrap_server(demo) || count_non_bootstrap_servers(demo) <= 1) {
        // ACLs are not necessary
        return 0;
    }

    const server_entry_t *server;
    DEMO_FOREACH_SERVER_ENTRY(server, demo->connection_args) {
        if (anjay_access_control_set_acl(demo->anjay, DEMO_OID_SERVER,
                                         (anjay_iid_t) server->id, server->id,
                                         ANJAY_ACCESS_MASK_READ
                                                 | ANJAY_ACCESS_MASK_WRITE
                                                 | ANJAY_ACCESS_MASK_EXECUTE)) {
            return -1;
        }
    }

    int result = 0;
    AVS_LIST(anjay_demo_object_t) object;
    AVS_LIST_FOREACH(object, demo->objects) {
        if ((*object->obj_ptr)->oid == DEMO_OID_SECURITY
                || (*object->obj_ptr)->oid == DEMO_OID_SERVER) {
            continue;
        }
        void *cookie = NULL;
        anjay_iid_t iid;
        while (!(result = (*object->obj_ptr)->handlers.instance_it(
                        demo->anjay, object->obj_ptr, &iid, &cookie))
                && iid != ANJAY_IID_INVALID) {
            if (anjay_access_control_set_acl(
                    demo->anjay,
                    (*object->obj_ptr)->oid,
                    iid,
                    ANJAY_SSID_ANY,
                    ANJAY_ACCESS_MASK_READ
                            | ANJAY_ACCESS_MASK_WRITE
                            | ANJAY_ACCESS_MASK_EXECUTE)) {
                return -1;
            }
        }
    }

    return result;
}

static int add_access_entries(anjay_demo_t *demo,
                              const cmdline_args_t *cmdline_args) {
    const AVS_LIST(access_entry_t) it;
    AVS_LIST_FOREACH(it, cmdline_args->access_entries) {
        if (anjay_access_control_set_acl(demo->anjay,
                                         it->oid, ANJAY_IID_INVALID, it->ssid,
                                         ANJAY_ACCESS_MASK_CREATE)) {
            return -1;
        }
    }
    return 0;
}

static int
install_object(anjay_demo_t *demo,
               const anjay_dm_object_def_t **obj_ptr,
               anjay_demo_object_notify_t *time_dependent_notify_func,
               anjay_demo_object_deleter_t *release_func) {
    if (!obj_ptr) {
        return -1;
    }

    AVS_LIST(anjay_demo_object_t) *object_entry =
            AVS_LIST_APPEND_PTR(&demo->objects);
    assert(object_entry && !*object_entry);
    *object_entry = AVS_LIST_NEW_ELEMENT(anjay_demo_object_t);
    if (!*object_entry) {
        release_func(obj_ptr);
        return -1;
    }

    if (anjay_register_object(demo->anjay, obj_ptr)) {
        release_func(obj_ptr);
        AVS_LIST_DELETE(object_entry);
        return -1;
    }

    (*object_entry)->obj_ptr = obj_ptr;
    (*object_entry)->time_dependent_notify_func = time_dependent_notify_func;
    (*object_entry)->release_func = release_func;
    return 0;
}

static int demo_init(anjay_demo_t *demo,
                     cmdline_args_t *cmdline_args) {

    anjay_configuration_t config = {
        .endpoint_name = cmdline_args->endpoint_name,
        .udp_listen_port = cmdline_args->udp_listen_port,
        .dtls_version = AVS_NET_SSL_VERSION_TLSv1_2,
        .in_buffer_size = (size_t) cmdline_args->inbuf_size,
        .out_buffer_size = (size_t) cmdline_args->outbuf_size,
        .msg_cache_size = (size_t) cmdline_args->msg_cache_size,
#ifndef IP_MTU
        .udp_socket_config = {
            .forced_mtu = 1492
        },
#endif
        .confirmable_notifications = cmdline_args->confirmable_notifications,
    };

    demo->connection_args = &cmdline_args->connection_args;

    demo->anjay = anjay_new(&config);
    demo->iosched = iosched_create();
    if (!demo->anjay
            || !demo->iosched
            || anjay_attr_storage_install(demo->anjay)
            || anjay_access_control_install(demo->anjay)
            || firmware_update_install(demo->anjay, &demo->fw_update,
                                       cmdline_args->fw_updated_marker_path)
            || !iosched_poll_entry_new(demo->iosched, STDIN_FILENO,
                                       POLLIN | POLLHUP,
                                       demo_command_dispatch, demo, NULL)) {
        return -1;
    }

    if (install_object(demo, anjay_security_object_create(), NULL,
                       anjay_security_object_delete)
            || install_object(demo, anjay_server_object_create(), NULL,
                              anjay_server_object_delete)
            || install_object(demo, location_object_create(),
                              location_notify_time_dependent,
                              location_object_release)
            || install_object(demo, apn_conn_profile_object_create(), NULL,
                              apn_conn_profile_object_release)
            || install_object(demo, cell_connectivity_object_create(demo), NULL,
                              cell_connectivity_object_release)
            || install_object(demo, cm_object_create(),
                              cm_notify_time_dependent, cm_object_release)
            || install_object(demo, cs_object_create(), NULL, cs_object_release)
            || install_object(demo, download_diagnostics_object_create(),
                              NULL, download_diagnostics_object_release)
            || install_object(demo,
                              device_object_create(demo->iosched,
                                                   cmdline_args->endpoint_name),
                              NULL, device_object_release)
            || install_object(demo, ext_dev_info_object_create(),
                              ext_dev_info_notify_time_dependent,
                              ext_dev_info_object_release)
            || install_object(demo, geopoints_object_create(demo),
                              geopoints_notify_time_dependent,
                              geopoints_object_release)
            || install_object(demo, ip_ping_object_create(demo->iosched), NULL,
                              ip_ping_object_release)
            || install_object(demo, test_object_create(),
                              test_notify_time_dependent,
                              test_object_release)
            || install_object(demo, portfolio_object_create(),
                              NULL, portfolio_object_release)) {
        return -1;
    }

    if (cmdline_args->location_csv
            && location_open_csv(demo_find_object(demo, DEMO_OID_LOCATION),
                                 cmdline_args->location_csv,
                                 cmdline_args->location_update_frequency_s)) {
        return -1;
    }

    demo_reload_servers(demo);

    if (add_default_access_entries(demo)
            || add_access_entries(demo, cmdline_args)) {
        return -1;
    }

    return 0;
}

static anjay_demo_t *demo_new(cmdline_args_t *cmdline_args) {
    anjay_demo_t *demo = (anjay_demo_t*) calloc(1, sizeof(anjay_demo_t));
    if (!demo) {
        return NULL;
    }

    if (demo_init(demo, cmdline_args)) {
        demo_delete(demo);
        return NULL;
    }

    demo->running = true;
    return demo;
}

static void notify_time_dependent(anjay_demo_t *demo) {
    anjay_demo_object_t *object;
    AVS_LIST_FOREACH(object, demo->objects) {
        if (object->time_dependent_notify_func) {
            object->time_dependent_notify_func(demo->anjay, object->obj_ptr);
        }
    }
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

    avs_time_real_t last_time = avs_time_real_now();

    while (demo->running) {
        refresh_socket_entries(demo, &socket_entries);
        demo_log(TRACE, "number of sockets to poll: %u",
                 (unsigned) AVS_LIST_SIZE(socket_entries));

        avs_time_real_t current_time = avs_time_real_now();

        if (current_time.since_real_epoch.seconds
                != last_time.since_real_epoch.seconds) {
            notify_time_dependent(demo);
        }
        last_time = current_time;

        int waitms = anjay_sched_calculate_wait_time_ms(
                demo->anjay,
                (int) ((1000500000 - current_time.since_real_epoch.nanoseconds)
                               / 1000000));
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
    assert(now_tm);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", now_tm);

    fprintf(stderr, "%s.%06d %s\n",
            timebuf, (int)now.tv_usec, message);
}

static void cmdline_args_cleanup(cmdline_args_t *cmdline_args) {
    free(cmdline_args->connection_args.public_cert_or_psk_identity);
    free(cmdline_args->connection_args.private_cert_or_psk_key);
    free(cmdline_args->connection_args.server_public_key);
    AVS_LIST_CLEAR(&cmdline_args->access_entries);
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

    // do not terminate after exceeding file size
    signal(SIGXFSZ, SIG_IGN);

    anjay_demo_t *demo = demo_new(&cmdline_args);
    if (!demo) {
        cmdline_args_cleanup(&cmdline_args);
        return -1;
    }

    // NOTE: This log is expected by our test suite (see Lwm2mTest.start_demo())
    // Please don't remove.
    demo_log(INFO, "*** ANJAY DEMO STARTUP FINISHED ***");
    serve(demo);
    demo_delete(demo);
    cmdline_args_cleanup(&cmdline_args);
    avs_log_reset();
    return 0;
}
