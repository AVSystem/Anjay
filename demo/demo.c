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

#include "demo.h"
#include "demo_args.h"
#include "demo_cmds.h"
#include "demo_utils.h"
#include "firmware_update.h"
#include "iosched.h"

#include "objects.h"
#include <avsystem/commons/avs_url.h>

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <unistd.h>

#include <avsystem/commons/avs_stream_file.h>

#include <anjay/access_control.h>
#include <anjay/attr_storage.h>
#include <anjay/fw_update.h>
#include <anjay/security.h>
#include <anjay/server.h>

static int security_object_reload(anjay_demo_t *demo) {
    anjay_security_object_purge(demo->anjay);
    const server_connection_args_t *args = demo->connection_args;
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
        instance.public_cert_or_psk_identity =
                args->public_cert_or_psk_identity;
        instance.public_cert_or_psk_identity_size =
                args->public_cert_or_psk_identity_size;
        instance.private_cert_or_psk_key = args->private_cert_or_psk_key;
        instance.private_cert_or_psk_key_size =
                args->private_cert_or_psk_key_size;
        instance.server_public_key = args->server_public_key;
        instance.server_public_key_size = args->server_public_key_size;

        anjay_iid_t iid = server->security_iid;
        if (anjay_security_object_add_instance(demo->anjay, &instance, &iid)) {
            demo_log(ERROR, "Cannot add Security Instance");
            return -1;
        }
    }
    return 0;
}

static int server_object_reload(anjay_demo_t *demo) {
    anjay_server_object_purge(demo->anjay);
    const server_entry_t *server;
    DEMO_FOREACH_SERVER_ENTRY(server, demo->connection_args) {
        if (server->is_bootstrap) {
            continue;
        }

        const anjay_server_instance_t instance = {
            .ssid = server->id,
            .lifetime = demo->connection_args->lifetime,
            .default_min_period = -1,
            .default_max_period = -1,
            .disable_timeout = -1,
            .binding = server->binding_mode,
            .notification_storing = true,
        };
        anjay_iid_t iid = server->server_iid;
        if (anjay_server_object_add_instance(demo->anjay, &instance, &iid)) {
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
    if (security_object_reload(demo) || server_object_reload(demo)) {
        demo_log(ERROR, "Error while adding new server objects");
        exit(-1);
    }
}

static void demo_delete(anjay_demo_t *demo) {
    if (demo->anjay && demo->attr_storage_file) {
        avs_stream_t *data = avs_stream_file_create(demo->attr_storage_file,
                                                    AVS_STREAM_FILE_WRITE);
        if (!data
                || avs_is_err(anjay_attr_storage_persist(demo->anjay, data))) {
            demo_log(ERROR, "Cannot persist attribute storage to file %s",
                     demo->attr_storage_file);
        }
        avs_stream_cleanup(&data);
    }

    if (demo->schedule_update_on_exit) {
        demo_log(INFO, "forced registration update on exit");
        if (demo->anjay) {
            anjay_schedule_registration_update(demo->anjay, ANJAY_SSID_ANY);
        } else {
            demo_log(INFO, "Anjay object not created, skipping");
        }
    }

    if (demo->anjay) {
        anjay_delete(demo->anjay);
    }
    AVS_LIST_CLEAR(&demo->objects) {
        demo->objects->release_func(demo->objects->obj_ptr);
    }
    firmware_update_destroy(&demo->fw_update);

    iosched_release(demo->iosched);
    AVS_LIST_CLEAR(&demo->allocated_strings);
    avs_free(demo);
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
                                         server->server_iid, server->id,
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
        AVS_LIST(anjay_iid_t) iids = NULL;
        result = object->get_instances_func(object->obj_ptr, &iids);
        AVS_LIST_CLEAR(&iids) {
            if (!result) {
                result = anjay_access_control_set_acl(
                        demo->anjay,
                        (*object->obj_ptr)->oid,
                        *iids,
                        ANJAY_SSID_ANY,
                        ANJAY_ACCESS_MASK_READ | ANJAY_ACCESS_MASK_WRITE
                                | ANJAY_ACCESS_MASK_EXECUTE);
            }
        }
    }

    return result;
}

static int add_access_entries(anjay_demo_t *demo,
                              const cmdline_args_t *cmdline_args) {
    const AVS_LIST(access_entry_t) it;
    AVS_LIST_FOREACH(it, cmdline_args->access_entries) {
        if (anjay_access_control_set_acl(demo->anjay, it->oid, ANJAY_ID_INVALID,
                                         it->ssid, ANJAY_ACCESS_MASK_CREATE)) {
            return -1;
        }
    }
    return 0;
}

static int get_single_instance(const anjay_dm_object_def_t **obj_ptr,
                               AVS_LIST(anjay_iid_t) *out) {
    (void) obj_ptr;
    assert(!*out);
    if (!(*out = AVS_LIST_NEW_ELEMENT(anjay_iid_t))) {
        demo_log(ERROR, "out of memory");
        return -1;
    }
    **out = 0;
    return 0;
}

static int
install_object(anjay_demo_t *demo,
               const anjay_dm_object_def_t **obj_ptr,
               anjay_demo_object_get_instances_t *get_instances_func,
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
    (*object_entry)->get_instances_func =
            (get_instances_func ? get_instances_func : get_single_instance);
    (*object_entry)->time_dependent_notify_func = time_dependent_notify_func;
    (*object_entry)->release_func = release_func;
    return 0;
}

static int demo_init(anjay_demo_t *demo, cmdline_args_t *cmdline_args) {

    anjay_configuration_t config = {
        .endpoint_name = cmdline_args->endpoint_name,
        .udp_listen_port = cmdline_args->udp_listen_port,
        .dtls_version = AVS_NET_SSL_VERSION_TLSv1_2,
        .in_buffer_size = (size_t) cmdline_args->inbuf_size,
        .out_buffer_size = (size_t) cmdline_args->outbuf_size,
        .msg_cache_size = (size_t) cmdline_args->msg_cache_size,
#ifndef IP_MTU
        .socket_config = {
            .forced_mtu = 1492
        },
#endif
        .confirmable_notifications = cmdline_args->confirmable_notifications,
        .disable_legacy_server_initiated_bootstrap =
                cmdline_args->disable_legacy_server_initiated_bootstrap,
        .udp_tx_params = &cmdline_args->tx_params,
        .udp_dtls_hs_tx_params = &cmdline_args->dtls_hs_tx_params,
        .stored_notification_limit = cmdline_args->stored_notification_limit,
        .prefer_hierarchical_formats =
                cmdline_args->prefer_hierarchical_formats,
        .use_connection_id = cmdline_args->use_connection_id,
        .default_tls_ciphersuites = {
            .ids = cmdline_args->default_ciphersuites,
            .num_ids = cmdline_args->default_ciphersuites_count
        },
    };

    const avs_net_security_info_t *fw_security_info_ptr = NULL;
    if (cmdline_args->fw_security_info.mode != (avs_net_security_mode_t) -1) {
        fw_security_info_ptr = &cmdline_args->fw_security_info;
    }

    demo->connection_args = &cmdline_args->connection_args;
    demo->attr_storage_file = cmdline_args->attr_storage_file;
    demo->anjay = anjay_new(&config);
    demo->iosched = iosched_create();
    if (!demo->anjay || !demo->iosched
            || anjay_attr_storage_install(demo->anjay)
            || anjay_access_control_install(demo->anjay)
            || firmware_update_install(
                       demo->anjay, demo->iosched, &demo->fw_update,
                       cmdline_args->fw_updated_marker_path,
                       fw_security_info_ptr,
                       cmdline_args->fwu_tx_params_modified
                               ? &cmdline_args->fwu_tx_params
                               : NULL,
                       cmdline_args->fw_update_delayed_result)) {
        return -1;
    }

#ifndef _WIN32
    if (!cmdline_args->disable_stdin) {
        if (!iosched_poll_entry_new(demo->iosched, STDIN_FILENO,
                                    POLLIN | POLLHUP, demo_command_dispatch,
                                    demo, NULL)) {
            return -1;
        }
    }
#else // _WIN32
#    warning "TODO: Support stdin somehow on Windows"
#endif // _WIN32

    if (anjay_security_object_install(demo->anjay)
            || anjay_server_object_install(demo->anjay)
            || install_object(demo, location_object_create(), NULL,
                              location_notify_time_dependent,
                              location_object_release)
            || install_object(demo, apn_conn_profile_object_create(),
                              apn_conn_profile_get_instances, NULL,
                              apn_conn_profile_object_release)
            || install_object(demo, binary_app_data_container_object_create(),
                              binary_app_data_container_get_instances, NULL,
                              binary_app_data_container_object_release)
            || install_object(demo, cell_connectivity_object_create(demo), NULL,
                              NULL, cell_connectivity_object_release)
            || install_object(demo, cm_object_create(), NULL,
                              cm_notify_time_dependent, cm_object_release)
            || install_object(demo, cs_object_create(), NULL, NULL,
                              cs_object_release)
            || install_object(demo, download_diagnostics_object_create(), NULL,
                              NULL, download_diagnostics_object_release)
            || install_object(demo,
                              device_object_create(demo->iosched,
                                                   cmdline_args->endpoint_name),
                              NULL, device_notify_time_dependent,
                              device_object_release)
            || install_object(demo, ext_dev_info_object_create(), NULL,
                              ext_dev_info_notify_time_dependent,
                              ext_dev_info_object_release)
            || install_object(demo, geopoints_object_create(demo),
                              geopoints_get_instances,
                              geopoints_notify_time_dependent,
                              geopoints_object_release)
#ifndef _WIN32
            || install_object(demo, ip_ping_object_create(demo->iosched), NULL,
                              NULL, ip_ping_object_release)
#endif // _WIN32
            || install_object(demo, test_object_create(), test_get_instances,
                              test_notify_time_dependent, test_object_release)
            || install_object(demo, portfolio_object_create(),
                              portfolio_get_instances, NULL,
                              portfolio_object_release)
            || install_object(demo, event_log_object_create(), NULL, NULL,
                              event_log_object_release)) {
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
    if (cmdline_args->attr_storage_file) {
        avs_stream_t *data =
                avs_stream_file_create(cmdline_args->attr_storage_file,
                                       AVS_STREAM_FILE_READ);
        if (!data
                || avs_is_err(anjay_attr_storage_restore(demo->anjay, data))) {
            demo_log(
                    ERROR,
                    "Cannot restore attribute storage persistence from file %s",
                    cmdline_args->attr_storage_file);
        }
        // no success log there, as Attribute Storage module logs it by itself
        avs_stream_cleanup(&data);
    }

    return 0;
}

static anjay_demo_t *demo_new(cmdline_args_t *cmdline_args) {
    anjay_demo_t *demo = (anjay_demo_t *) avs_calloc(1, sizeof(anjay_demo_t));
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
    avs_net_socket_t *socket;
    const iosched_entry_t *iosched_entry;
} socket_entry_t;

static void socket_dispatch(short revents, void *arg_) {
    (void) revents;
    socket_entry_t *arg = (socket_entry_t *) arg_;
    int result = anjay_serve(arg->demo->anjay, arg->socket);
    demo_log(DEBUG, "anjay_serve returned %d", result);
}

static socket_entry_t *create_socket_entry(anjay_demo_t *demo,
                                           avs_net_socket_t *socket) {
    socket_entry_t *entry = AVS_LIST_NEW_ELEMENT(socket_entry_t);
    if (!entry) {
        demo_log(ERROR, "out of memory");
        return NULL;
    }

    const demo_fd_t *sys_socket =
            (const demo_fd_t *) avs_net_socket_get_system(socket);
    if (!sys_socket) {
        demo_log(ERROR, "could not obtain system socket");
        AVS_LIST_DELETE(&entry);
        return NULL;
    }
    entry->demo = demo;
    entry->socket = socket;
    entry->iosched_entry =
            iosched_poll_entry_new(demo->iosched, *sys_socket, POLLIN,
                                   socket_dispatch, entry, NULL);
    if (!entry->iosched_entry) {
        demo_log(ERROR, "cannot add iosched entry");
        AVS_LIST_DELETE(&entry);
    }
    return entry;
}

static void refresh_socket_entries(anjay_demo_t *demo,
                                   AVS_LIST(socket_entry_t) *entry_ptr) {
    AVS_LIST(avs_net_socket_t *const) sockets = anjay_get_sockets(demo->anjay);

    AVS_LIST(avs_net_socket_t *const) socket;
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

static avs_sched_t *get_event_log_sched(anjay_demo_t *demo) {
    const anjay_dm_object_def_t **obj_def =
            demo_find_object(demo, DEMO_OID_EVENT_LOG);
    return obj_def ? event_log_get_sched(obj_def) : NULL;
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

        avs_time_duration_t event_log_wait_duration =
                avs_sched_time_to_next(get_event_log_sched(demo));

        if (avs_time_duration_valid(event_log_wait_duration)) {
            int64_t event_log_waitms;
            avs_time_duration_to_scalar(&event_log_waitms, AVS_TIME_MS,
                                        event_log_wait_duration);
            waitms = AVS_MIN((int) event_log_waitms, waitms);
        }

        // +1 to prevent annoying looping in case of
        // sub-millisecond delays
        iosched_run(demo->iosched, waitms + 1);

        anjay_sched_run(demo->anjay);
        avs_sched_run(get_event_log_sched(demo));
    }

    AVS_LIST_CLEAR(&socket_entries) {
        iosched_entry_remove(demo->iosched, socket_entries->iosched_entry);
    }
}

static void
log_handler(avs_log_level_t level, const char *module, const char *message) {
    (void) level;
    (void) module;

    char timebuf[128];
    avs_time_real_t now = avs_time_real_now();
    time_t seconds = now.since_real_epoch.seconds;

    struct tm *now_tm = localtime(&seconds);
    assert(now_tm);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", now_tm);

    fprintf(stderr, "%s.%06d %s\n", timebuf,
            (int) now.since_real_epoch.nanoseconds / 1000, message);
}

static void cmdline_args_cleanup(cmdline_args_t *cmdline_args) {
    avs_free(cmdline_args->connection_args.public_cert_or_psk_identity);
    avs_free(cmdline_args->connection_args.private_cert_or_psk_key);
    avs_free(cmdline_args->connection_args.server_public_key);
    AVS_LIST_CLEAR(&cmdline_args->access_entries);
    avs_free(cmdline_args->default_ciphersuites);
}

int main(int argc, char *argv[]) {
#ifndef _WIN32
    /*
     * The demo application implements mock firmware update with execv() call
     * on the new LwM2M client application. As a direct consequence, all file
     * descriptors from the original process are inherited, even though we will
     * never use most of them. To free resources associated with these
     * descriptors and avoid weird behavior caused by multiple sockets bound to
     * the same local port (*), we close all unknown descriptors before
     * continuing. Only 0 (stdin), 1 (stdout) and 2 (stderr) are left open.
     *
     * (*) For example, Linux does load-balancing between UDP sockets that
     * reuse the same local address and port. See `man 7 socket` or
     * http://man7.org/linux/man-pages/man7/socket.7.html .
     * https://stackoverflow.com/a/14388707/2339636 contains more detailed
     * info on SO_REUSEADDR/SO_REUSEPORT behavior on various systems.
     */
    for (int fd = 3, maxfd = (int) sysconf(_SC_OPEN_MAX); fd < maxfd; ++fd) {
        close(fd);
    }
#endif // WIN32

    /*
     * If, as a result of a single poll() more than a single line is read into
     * stdin buffer, we will end up handling just a single command and then
     * wait for another poll() trigger which may never happen - because all the
     * data from fd 0 was already read, and it's just waiting to be read from
     * the buffer.
     *
     * This problematic behavior can be reproduced by sending a "\ncommand\n"
     * string to the demo application with a single write() syscall.
     *
     * Disabling stdin buffering prevents Python tests from hanging randomly.
     * While generally that is not a good idea performance-wise, demo commands
     * do not require passing large amounts of data, so it is fine in our use
     * case.
     */
    setbuf(stdin, NULL);
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    avs_log_set_handler(log_handler);
    avs_log_set_default_level(AVS_LOG_TRACE);
    avs_log_set_level(demo, AVS_LOG_DEBUG);
    avs_log_set_level(avs_sched, AVS_LOG_DEBUG);
    avs_log_set_level(anjay_dm, AVS_LOG_DEBUG);

    if (argv_store(argc, argv)) {
        return -1;
    }

    cmdline_args_t cmdline_args;
    if (demo_parse_argv(&cmdline_args, argc, argv)) {
        return -1;
    }

#ifdef SIGXFSZ
    // do not terminate after exceeding file size
    signal(SIGXFSZ, SIG_IGN);
#endif // SIGXFSZ

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
