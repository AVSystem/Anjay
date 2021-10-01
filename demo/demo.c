/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    define _WIN32_WINNT \
        0x600 // minimum requirement: Windows NT 6.0 a.k.a. Vista
#    include <ws2tcpip.h>
#    undef ERROR
#else // _WIN32
#    include <netinet/in.h>
#endif // _WIN32

#include "demo.h"
#include "demo_args.h"
#include "demo_cmds.h"
#include "demo_utils.h"
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
#    include "firmware_update.h"
#endif // ANJAY_WITH_MODULE_FW_UPDATE

#include "objects.h"
#include <avsystem/commons/avs_url.h>

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <avsystem/commons/avs_base64.h>
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
#ifdef ANJAY_WITH_BOOTSTRAP
            instance.client_holdoff_s = args->bootstrap_holdoff_s;
            instance.bootstrap_timeout_s = args->bootstrap_timeout_s;
#endif // ANJAY_WITH_BOOTSTRAP
        } else {
            instance.client_holdoff_s = -1;
            instance.bootstrap_timeout_s = -1;
            instance.ssid = server->id;
        }
        static const char SECURE_PREFIX[] = "coaps";
        if (server->uri
                && strncmp(server->uri, SECURE_PREFIX, strlen(SECURE_PREFIX))
                               == 0) {
            instance.security_mode = args->security_mode;
        } else {
            instance.security_mode = ANJAY_SECURITY_NOSEC;
        }

        /**
         * Note: we can assign pointers by value, as @ref
         * anjay_security_object_add_instance will make a deep copy by itself.
         */
        instance.server_uri = server->uri;
        if (instance.security_mode != ANJAY_SECURITY_EST
                || server->is_bootstrap) {
            instance.public_cert_or_psk_identity =
                    args->public_cert_or_psk_identity;
            instance.public_cert_or_psk_identity_size =
                    args->public_cert_or_psk_identity_size;
            instance.private_cert_or_psk_key = args->private_cert_or_psk_key;
            instance.private_cert_or_psk_key_size =
                    args->private_cert_or_psk_key_size;
        }
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
    avs_sched_del(&demo->notify_time_dependent_job);

#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
#    ifdef ANJAY_WITH_MODULE_ATTR_STORAGE
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
#    endif // ANJAY_WITH_MODULE_ATTR_STORAGE

    if (demo->anjay && demo->dm_persistence_file) {
        avs_stream_t *data = avs_stream_file_create(demo->dm_persistence_file,
                                                    AVS_STREAM_FILE_WRITE);
        if (!data
                || avs_is_err(anjay_security_object_persist(demo->anjay, data))
                || avs_is_err(anjay_server_object_persist(demo->anjay, data))
#    ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
                || avs_is_err(anjay_access_control_persist(demo->anjay, data))
#    endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
        ) {
            demo_log(ERROR, "Cannot persist data model to file %s",
                     demo->dm_persistence_file);
        }
        avs_stream_cleanup(&data);
    }
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)

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
    AVS_LIST_CLEAR(&demo->installed_objects_update_handlers);
#ifdef ANJAY_WITH_MODULE_FW_UPDATE
    firmware_update_destroy(&demo->fw_update);
#endif // ANJAY_WITH_MODULE_FW_UPDATE

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

#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
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
        if (anjay_access_control_set_acl(demo->anjay, it->oid, it->iid,
                                         it->ssid, it->mask)) {
            return -1;
        }
    }
    return 0;
}
#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL

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

static int
add_installed_object_update_handler(anjay_demo_t *demo,
                                    anjay_update_handler_t *handler) {
    assert(demo);

    AVS_LIST(anjay_update_handler_t *) *handler_entry =
            AVS_LIST_APPEND_PTR(&demo->installed_objects_update_handlers);
    assert(handler_entry && !*handler_entry);
    *handler_entry = AVS_LIST_NEW_ELEMENT(anjay_update_handler_t *);
    **handler_entry = handler;

    return 0;
}

static void reschedule_notify_time_dependent(anjay_demo_t *demo);

static void notify_time_dependent_job(avs_sched_t *sched,
                                      const void *demo_ptr) {
    (void) sched;
    anjay_demo_t *demo = *(anjay_demo_t *const *) demo_ptr;
    anjay_demo_object_t *object;
    AVS_LIST_FOREACH(object, demo->objects) {
        if (object->time_dependent_notify_func) {
            object->time_dependent_notify_func(demo->anjay, object->obj_ptr);
        }
    }
    anjay_update_handler_t **update_handler;
    AVS_LIST_FOREACH(update_handler, demo->installed_objects_update_handlers) {
        (*update_handler)(demo->anjay);
    }
    reschedule_notify_time_dependent(demo);
}

static void reschedule_notify_time_dependent(anjay_demo_t *demo) {
    avs_time_real_t now = avs_time_real_now();
    avs_time_real_t next_full_second = {
        .since_real_epoch = {
            .seconds = now.since_real_epoch.seconds + 1,
            .nanoseconds = 0
        }
    };
    AVS_SCHED_DELAYED(anjay_get_scheduler(demo->anjay),
                      &demo->notify_time_dependent_job,
                      avs_time_real_diff(next_full_second, now),
                      notify_time_dependent_job, &demo, sizeof(demo));
}

static int demo_init(anjay_demo_t *demo, cmdline_args_t *cmdline_args) {
    for (size_t i = 0; i < MAX_SERVERS; ++i) {
        server_entry_t *entry = &cmdline_args->connection_args.servers[i];
        if (entry->uri == NULL) {
            break;
        }

        if (entry->binding_mode == NULL) {
            entry->binding_mode = "U";
        }
    }

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

#ifdef ANJAY_WITH_MODULE_FW_UPDATE
    const avs_net_security_info_t *fw_security_info_ptr = NULL;
    if (cmdline_args->fw_security_info.mode != (avs_net_security_mode_t) -1) {
        fw_security_info_ptr = &cmdline_args->fw_security_info;
    }
#endif // ANJAY_WITH_MODULE_FW_UPDATE

    demo->connection_args = &cmdline_args->connection_args;
#ifdef AVS_COMMONS_STREAM_WITH_FILE
#    ifdef ANJAY_WITH_MODULE_ATTR_STORAGE
    demo->attr_storage_file = cmdline_args->attr_storage_file;
#    endif // ANJAY_WITH_MODULE_ATTR_STORAGE
#    ifdef AVS_COMMONS_WITH_AVS_PERSISTENCE
    demo->dm_persistence_file = cmdline_args->dm_persistence_file;
#    endif // AVS_COMMONS_WITH_AVS_PERSISTENCE
#endif     // AVS_COMMONS_STREAM_WITH_FILE
    demo->anjay = anjay_new(&config);
    if (!demo->anjay
#ifdef ANJAY_WITH_MODULE_ATTR_STORAGE
            || anjay_attr_storage_install(demo->anjay)
#endif // ANJAY_WITH_MODULE_ATTR_STORAGE
#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
            || anjay_access_control_install(demo->anjay)
#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
    ) {
        return -1;
    }

    if (anjay_security_object_install(demo->anjay)
            || anjay_server_object_install(demo->anjay)
#ifdef ANJAY_WITH_MODULE_IPSO_OBJECTS
            || install_accelerometer_object(demo->anjay)
            || add_installed_object_update_handler(demo,
                                                   accelerometer_update_handler)
            || install_push_button_object(demo->anjay)
            || install_temperature_object(demo->anjay)
            || add_installed_object_update_handler(demo,
                                                   temperature_update_handler)
#endif // ANJAY_WITH_MODULE_IPSO_OBJECTS
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
                              device_object_create(cmdline_args->endpoint_name),
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
            || install_object(demo, ip_ping_object_create(), NULL, NULL,
                              ip_ping_object_release)
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

    bool dm_persistence_restored = false;
#if defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
    if (cmdline_args->dm_persistence_file) {
        avs_stream_t *data =
                avs_stream_file_create(cmdline_args->dm_persistence_file,
                                       AVS_STREAM_FILE_READ);
        if (!data
                || avs_is_err(anjay_security_object_restore(demo->anjay, data))
                || avs_is_err(anjay_server_object_restore(demo->anjay, data))
#    ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
                || avs_is_err(anjay_access_control_restore(demo->anjay, data))
#    endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
        ) {
            demo_log(ERROR, "Cannot restore data model from file %s",
                     cmdline_args->dm_persistence_file);
        } else {
            dm_persistence_restored = true;
        }
        avs_stream_cleanup(&data);
    }
#endif // defined(AVS_COMMONS_WITH_AVS_PERSISTENCE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)

    if (!dm_persistence_restored) {
        demo_reload_servers(demo);
    }

#ifdef ANJAY_WITH_MODULE_FW_UPDATE
    // Install Firmware Update Object at the end, because installed Device
    // Object and Server Object's instances may be needed.
    if (firmware_update_install(demo->anjay, &demo->fw_update,
                                cmdline_args->fw_updated_marker_path,
                                fw_security_info_ptr,
                                cmdline_args->fwu_tx_params_modified
                                        ? &cmdline_args->fwu_tx_params
                                        : NULL,
                                cmdline_args->fw_update_delayed_result)) {
        return -1;
    }
#endif // ANJAY_WITH_MODULE_FW_UPDATE

#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
    if (!dm_persistence_restored
            && (add_default_access_entries(demo)
                || add_access_entries(demo, cmdline_args))) {
        return -1;
    }
#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL

#if defined(ANJAY_WITH_MODULE_ATTR_STORAGE) \
        && defined(AVS_COMMONS_STREAM_WITH_FILE)
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
#endif // defined(ANJAY_WITH_MODULE_ATTR_STORAGE) &&
       // defined(AVS_COMMONS_STREAM_WITH_FILE)

    reschedule_notify_time_dependent(demo);

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

    return demo;
}

static void *event_loop_func(void *demo) {
    // NOTE: This log is expected by our test suite (see Lwm2mTest.start_demo())
    // Please don't remove.
    demo_log(INFO, "*** ANJAY DEMO STARTUP FINISHED ***");
    int result = anjay_event_loop_run(
            ((anjay_demo_t *) demo)->anjay,
            avs_time_duration_from_scalar(100, AVS_TIME_MS));
    // force the stdin reading loop to finish
    close(STDIN_FILENO);
    return (void *) (intptr_t) result;
}

static void interrupt_event_loop_job(avs_sched_t *sched, const void *demo_ptr) {
    (void) sched;
    anjay_demo_t *demo = *(anjay_demo_t *const *) demo_ptr;
    anjay_event_loop_interrupt(demo->anjay);
}

static void log_extended_handler(avs_log_level_t level,
                                 const char *module,
                                 const char *file,
                                 unsigned line,
                                 const char *message) {
    static const char *log_levels[] = { "TRC", "DBG", "INF", "WRN", "ERR", "" };
    char *name = strrchr(file, '/');

    if (name) {
        char file_name[30];
        snprintf(file_name, sizeof(file_name), "%s:%d", name + 1, line);
        fprintf(stderr, "%s: |%-15s| %-30s| %s\n", log_levels[level], module,
                file_name, message);
    } else {
        fprintf(stderr, "%s: |%-15s| %s:%d| %s\n", log_levels[level], module,
                file, line, message);
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
#ifdef ANJAY_WITH_MODULE_ACCESS_CONTROL
    AVS_LIST_CLEAR(&cmdline_args->access_entries);
#endif // ANJAY_WITH_MODULE_ACCESS_CONTROL
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

    if (cmdline_args.alternative_logger) {
        avs_log_set_extended_handler(log_extended_handler);
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

    pthread_t event_loop_thread;
    if (!pthread_create(&event_loop_thread, NULL, event_loop_func, demo)) {
        if (!cmdline_args.disable_stdin) {
            union {
                demo_command_invocation_t invocation;
                char buf[offsetof(demo_command_invocation_t, cmd) + 500];
            } invocation = {
                .invocation.demo = demo
            };
            while (!feof(stdin) && !ferror(stdin)) {
                if (fgets(invocation.invocation.cmd,
                          sizeof(invocation)
                                  - offsetof(demo_command_invocation_t, cmd),
                          stdin)) {
                    while (true) {
                        size_t buf_len = strlen(invocation.invocation.cmd);
                        if (!buf_len) {
                            break;
                        }
                        char *last_char =
                                &invocation.invocation.cmd[buf_len - 1];
                        if (*last_char == '\r' || *last_char == '\n') {
                            *last_char = '\0';
                        } else {
                            break;
                        }
                    }
                    demo_command_dispatch(&invocation.invocation);
                }
            }
            // NOTE: anjay_event_loop_interrupt() intentionally does not work if
            // called before the event loop actually starts; it means that we
            // can't call it directly hear, as it would lead to a race condition
            // if stdin is closed immediately (e.g. is /dev/null).
            AVS_SCHED_NOW(anjay_get_scheduler(demo->anjay), NULL,
                          interrupt_event_loop_job, &demo, sizeof(demo));
        }

        pthread_join(event_loop_thread, NULL);
    }

    demo_delete(demo);
    cmdline_args_cleanup(&cmdline_args);
    avs_log_reset();
    return 0;
}
