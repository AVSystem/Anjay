/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#include "demo_cmds.h"
#include "demo.h"
#include "demo_utils.h"
#include "firmware_update.h"

#include <ctype.h>
#include <inttypes.h>
#include <string.h>

#include <anjay/attr_storage.h>
#include <anjay/security.h>

#include <avsystem/commons/memory.h>

static int parse_ssid(const char *text, anjay_ssid_t *out_ssid) {
    unsigned id;
    if (sscanf(text, "%u", &id) < 1 || id > UINT16_MAX) {
        demo_log(ERROR, "invalid Short Server ID: %s", text);
        return -1;
    }
    *out_ssid = (uint16_t) id;
    return 0;
}

static void cmd_send_update(anjay_demo_t *demo, const char *args_string) {
    anjay_ssid_t ssid = ANJAY_SSID_ANY;
    if (*args_string && parse_ssid(args_string, &ssid)) {
        return;
    }

    if (anjay_schedule_registration_update(demo->anjay, ssid)) {
        demo_log(ERROR, "could not schedule registration update");
    } else if (ssid == ANJAY_SSID_ANY) {
        demo_log(INFO, "registration update scheduled for all servers");
    } else {
        demo_log(INFO, "registration update scheduled for server %u", ssid);
    }
}

static void cmd_reconnect(anjay_demo_t *demo, const char *args_string) {
    (void) args_string;

    if (anjay_schedule_reconnect(demo->anjay)) {
        demo_log(ERROR, "could not schedule reconnect");
    } else {
        demo_log(INFO, "reconnect scheduled for all servers");
    }
}

static void cmd_set_fw_package_path(anjay_demo_t *demo,
                                    const char *args_string) {
    const char *path = args_string;
    while (isspace(*path)) {
        ++path;
    }

    firmware_update_set_package_path(&demo->fw_update, path);
}

static void cmd_open_location_csv(anjay_demo_t *demo, const char *args_string) {
    const anjay_dm_object_def_t **location_obj =
            demo_find_object(demo, DEMO_OID_LOCATION);
    if (!location_obj) {
        demo_log(ERROR, "Location object not registered");
        return;
    }

    char *filename = (char *) avs_malloc(strlen(args_string) + 1);
    if (!filename) {
        demo_log(ERROR, "Out of memory");
        return;
    }
    filename[0] = '\0';
    unsigned long frequency_s = 1;
    sscanf(args_string, "%s %lu", filename, &frequency_s);
    if (!location_open_csv(location_obj, filename, (time_t) frequency_s)) {
        demo_log(INFO, "Successfully opened CSV file");
    }
    avs_free(filename);
}

static size_t count_servers(const server_connection_args_t *args) {
    size_t num_servers = 0;
    const server_entry_t *server;
    DEMO_FOREACH_SERVER_ENTRY(server, args) {
        ++num_servers;
    }
    return num_servers;
}

static int add_server(anjay_demo_t *demo, const char *uri) {
    size_t num_servers = count_servers(demo->connection_args);
    if (num_servers >= MAX_SERVERS) {
        demo_log(ERROR, "Maximum number of servers reached");
        return -1;
    }
    size_t uri_size = strlen(uri) + 1;
    AVS_LIST(anjay_demo_string_t) copied_uri =
            (AVS_LIST(anjay_demo_string_t)) AVS_LIST_NEW_BUFFER(uri_size);
    if (!copied_uri) {
        demo_log(ERROR, "Out of memory");
        return -1;
    }
    memcpy(copied_uri->data, uri, uri_size);
    AVS_LIST_INSERT(&demo->allocated_strings, copied_uri);

    server_entry_t *entry = &demo->connection_args->servers[num_servers];
    *entry = demo->connection_args->servers[num_servers - 1];
    entry->id = (anjay_ssid_t) (num_servers + 1);
    entry->uri = copied_uri->data;
    entry->security_iid = (anjay_iid_t) entry->id;
    entry->server_iid = (anjay_iid_t) entry->id;
    demo_log(INFO, "Added new server, ID == %d", (int) (num_servers + 1));
    return 0;
}

static void cmd_add_server(anjay_demo_t *demo, const char *args_string) {
    const char *uri = args_string;
    while (isspace(*uri)) {
        ++uri;
    }

    if (add_server(demo, uri)) {
        return;
    }
    demo_reload_servers(demo);
}

static void cmd_trim_servers(anjay_demo_t *demo, const char *args_string) {
    size_t num_servers = count_servers(demo->connection_args);
    unsigned number;
    if (sscanf(args_string, "%u", &number) != 1 || number >= num_servers) {
        demo_log(ERROR, "Invalid servers number: %s", args_string);
        return;
    }

    for (size_t i = number; i < num_servers; ++i) {
        demo->connection_args->servers[i].uri = NULL;
    }
    demo_reload_servers(demo);
}

static void cmd_socket_count(anjay_demo_t *demo, const char *args_string) {
    (void) args_string;
    printf("SOCKET_COUNT==%lu\n",
           (unsigned long) AVS_LIST_SIZE(anjay_get_sockets(demo->anjay)));
}

static void cmd_get_port(anjay_demo_t *demo, const char *args_string) {
    int index;
    if (sscanf(args_string, "%d", &index) != 1) {
        demo_log(ERROR, "Invalid index: %s", args_string);
        return;
    }

    AVS_LIST(avs_net_abstract_socket_t *const) sockets =
            anjay_get_sockets(demo->anjay);
    int num_sockets = (int) AVS_LIST_SIZE(sockets);
    if (index < 0) {
        index = num_sockets + index;
    }
    if (index < 0 || index >= num_sockets) {
        demo_log(ERROR, "Index out of range: %d; num_sockets == %d", index,
                 num_sockets);
    }
    char port[16] = "0";
    AVS_LIST(avs_net_abstract_socket_t *const) socket =
            AVS_LIST_NTH(sockets, (size_t) index);
    if (socket && *socket) {
        avs_net_socket_get_local_port(*socket, port, sizeof(port));
    }
    printf("PORT==%s\n", port);
}

static void cmd_get_transport(anjay_demo_t *demo, const char *args_string) {
    int index;
    if (sscanf(args_string, "%d", &index) != 1) {
        demo_log(ERROR, "Invalid index: %s", args_string);
        return;
    }

    AVS_LIST(const anjay_socket_entry_t) entries =
            anjay_get_socket_entries(demo->anjay);
    int num_sockets = (int) AVS_LIST_SIZE(entries);
    if (index < 0) {
        index = num_sockets + index;
    }
    if (index < 0 || index >= num_sockets) {
        demo_log(ERROR, "Index out of range: %d; num_sockets == %d", index,
                 num_sockets);
    }
    AVS_LIST(const anjay_socket_entry_t) entry =
            AVS_LIST_NTH(entries, (size_t) index);
    switch (entry->transport) {
    case ANJAY_SOCKET_TRANSPORT_UDP:
        puts("TRANSPORT==UDP");
        break;
    case ANJAY_SOCKET_TRANSPORT_TCP:
        puts("TRANSPORT==TCP");
        break;
    case ANJAY_SOCKET_TRANSPORT_SMS:
        puts("TRANSPORT==SMS");
        break;
    default:
        printf("TRANSPORT==%d\n", (int) entry->transport);
    }
}

static void cmd_non_lwm2m_socket_count(anjay_demo_t *demo,
                                       const char *args_string) {
    (void) args_string;
    AVS_LIST(const anjay_socket_entry_t) entry =
            anjay_get_socket_entries(demo->anjay);
    unsigned long non_lwm2m_sockets = 0;
    AVS_LIST_ITERATE(entry) {
        if (entry->ssid == ANJAY_SSID_ANY
                && entry->transport != ANJAY_SOCKET_TRANSPORT_SMS) {
            ++non_lwm2m_sockets;
        }
    }
    printf("NON_LWM2M_SOCKET_COUNT==%lu\n", non_lwm2m_sockets);
}

static void cmd_enter_offline(anjay_demo_t *demo, const char *args_string) {
    (void) args_string;
    int result = anjay_enter_offline(demo->anjay);
    demo_log(INFO, "anjay_enter_offline(), result == %d", result);
}

static void cmd_exit_offline(anjay_demo_t *demo, const char *args_string) {
    (void) args_string;
    int result = anjay_exit_offline(demo->anjay);
    demo_log(INFO, "anjay_exit_offline(), result == %d", result);
}

static void cmd_notify(anjay_demo_t *demo, const char *args_string) {
    anjay_oid_t oid;
    anjay_iid_t iid;
    anjay_rid_t rid;
    if (sscanf(args_string, " /%hu/%hu/%hu", &oid, &iid, &rid) == 3) {
        (void) anjay_notify_changed(demo->anjay, oid, iid, rid);
    } else if (sscanf(args_string, " /%hu", &oid) == 1) {
        (void) anjay_notify_instances_changed(demo->anjay, oid);
    } else {
        demo_log(WARNING, "notify usage:\n"
                          "1. notify /OID\n"
                          "2. notify /OID/IID/RID");
        return;
    }
}

static void cmd_unregister_object(anjay_demo_t *demo, const char *args_string) {
    int oid;
    if (sscanf(args_string, "%d", &oid) != 1 || oid < 0 || oid > UINT16_MAX) {
        demo_log(ERROR, "Invalid OID: %s", args_string);
        return;
    }

    AVS_LIST(anjay_demo_object_t) *object_entry_ptr;
    AVS_LIST_FOREACH_PTR(object_entry_ptr, &demo->objects) {
        if ((*(*object_entry_ptr)->obj_ptr)->oid == oid) {
            if (anjay_unregister_object(demo->anjay,
                                        (*object_entry_ptr)->obj_ptr)) {
                demo_log(ERROR, "Could not unregister object %d", oid);
                return;
            }
            (*object_entry_ptr)->release_func((*object_entry_ptr)->obj_ptr);
            AVS_LIST_DELETE(object_entry_ptr);
            return;
        }
    }

    demo_log(ERROR, "No such object to unregister: %d", oid);
}

static int dl_write_next_block(anjay_t *anjay,
                               const uint8_t *data,
                               size_t data_size,
                               const anjay_etag_t *etag,
                               void *user_data) {
    (void) anjay;
    (void) etag;

    FILE *f = (FILE *) user_data;
    if (fwrite(data, data_size, 1, f) != 1) {
        demo_log(ERROR, "fwrite() failed");
        return -1;
    }

    return 0;
}

static void dl_finished(anjay_t *anjay, int result, void *user_data) {
    (void) anjay;
    fclose((FILE *) user_data);
    demo_log(INFO, "download finished, result == %d", result);
}

static void cmd_download(anjay_demo_t *demo, const char *args_string) {
    char url[256];
    char target_file[256];
    char psk_identity[256] = "";
    char psk_key[256] = "";

    if (sscanf(args_string, "%255s %255s %255s %255s", url, target_file,
               psk_identity, psk_key)
            < 2) {
        demo_log(ERROR, "invalid URL or target file in: %s", args_string);
        return;
    }

    FILE *f = fopen(target_file, "wb");
    if (!f) {
        demo_log(ERROR, "could not open file: %s", target_file);
        return;
    }

    avs_net_psk_info_t psk = {
        .psk = psk_key,
        .psk_size = strlen(psk_key),
        .identity = psk_identity,
        .identity_size = strlen(psk_identity)
    };
    anjay_download_config_t cfg = {
        .url = url,
        .on_next_block = dl_write_next_block,
        .on_download_finished = dl_finished,
        .user_data = f,
        .security_info = avs_net_security_info_from_psk(psk)
    };

    if (anjay_download(demo->anjay, &cfg) == NULL) {
        demo_log(ERROR, "could not schedule download");
        fclose(f);
    }
}

static void cmd_set_attrs(anjay_demo_t *demo, const char *args_string) {
    char *path = (char *) avs_malloc(strlen(args_string) + 1);
    if (!path) {
        demo_log(ERROR, "Out of memory");
        return;
    }
    int path_len = 0;
    const char *args = NULL, *pmin = NULL, *pmax = NULL, *lt = NULL, *gt = NULL,
               *st = NULL;
    anjay_dm_resource_attributes_t attrs;
    int ssid;

    if (sscanf(args_string, "%s %d%n", path, &ssid, &path_len) != 2) {
        goto error;
    }

    if (ssid < 0 || UINT16_MAX <= ssid) {
        demo_log(ERROR, "invalid SSID: expected 0 <= ssid < 65535, got %d",
                 ssid);
        goto error;
    }

    args = args_string + path_len;
    attrs = ANJAY_RES_ATTRIBS_EMPTY;
    pmin = strstr(args, "pmin=");
    pmax = strstr(args, "pmax=");
    lt = strstr(args, "lt=");
    gt = strstr(args, "gt=");
    st = strstr(args, "st=");
    if (pmin) {
        (void) sscanf(pmin, "pmin=%" PRId32, &attrs.common.min_period);
    }
    if (pmax) {
        (void) sscanf(pmax, "pmax=%" PRId32, &attrs.common.max_period);
    }
    if (lt) {
        (void) sscanf(lt, "lt=%lf", &attrs.less_than);
    }
    if (gt) {
        (void) sscanf(gt, "gt=%lf", &attrs.greater_than);
    }
    if (st) {
        (void) sscanf(st, "st=%lf", &attrs.step);
    }

    int oid, iid, rid;
    switch (sscanf(path, "/%d/%d/%d", &oid, &iid, &rid)) {
    case 3:
        if (anjay_attr_storage_set_resource_attrs(
                    demo->anjay, (anjay_ssid_t) ssid, (anjay_oid_t) oid,
                    (anjay_iid_t) iid, (anjay_rid_t) rid, &attrs)) {
            demo_log(ERROR, "failed to set resource level attributes");
        }
        goto finish;
    case 2:
        if (anjay_attr_storage_set_instance_attrs(
                    demo->anjay, (anjay_ssid_t) ssid, (anjay_oid_t) oid,
                    (anjay_iid_t) iid, &attrs.common)) {
            demo_log(ERROR, "failed to set instance level attributes");
        }
        goto finish;
    case 1:
        if (anjay_attr_storage_set_object_attrs(
                    demo->anjay, (anjay_ssid_t) ssid, (anjay_oid_t) oid,
                    &attrs.common)) {
            demo_log(ERROR, "failed to set object level attributes");
        }
        goto finish;
    }
error:
    demo_log(ERROR, "bad syntax - see help");
finish:
    avs_free(path);
}

static void cmd_enable_server(anjay_demo_t *demo, const char *args_string) {
    anjay_ssid_t ssid = ANJAY_SSID_ANY;
    if (*args_string && parse_ssid(args_string, &ssid)) {
        return;
    }

    if (anjay_enable_server(demo->anjay, ssid)) {
        demo_log(ERROR, "could not enable server with SSID %" PRIu16, ssid);
        return;
    }
}

static void cmd_all_connections_failed(anjay_demo_t *demo,
                                       const char *unused_args) {
    (void) unused_args;
    printf("ALL_CONNECTIONS_FAILED==%d\n",
           (int) anjay_all_connections_failed(demo->anjay));
}

static void cmd_schedule_update_on_exit(anjay_demo_t *demo,
                                        const char *unused_args) {
    (void) unused_args;
    demo->schedule_update_on_exit = true;
}

static void cmd_help(anjay_demo_t *demo, const char *args_string);

struct cmd_handler_def {
    const char *cmd_name;
    size_t cmd_name_length;
    void (*handler)(anjay_demo_t *, const char *);
    const char *help_args;
    const char *help_descr;
};

#define CMD_HANDLER(name, args, func, help) \
    { (name), sizeof(name) - 1, (func), (args), (help) }
static const struct cmd_handler_def COMMAND_HANDLERS[] = {
    // clang-format off
    CMD_HANDLER("send-update", "[ssid=0]",
                cmd_send_update, "Sends Update messages to LwM2M servers"),
    CMD_HANDLER("reconnect", "", cmd_reconnect,
                "Reconnects to LwM2M servers and sends Update messages"),
    CMD_HANDLER("set-fw-package-path", "", cmd_set_fw_package_path,
                "Sets the path where the firmware package will be saved when "
                "Write /5/0/0 is performed"),
    CMD_HANDLER("open-location-csv", "filename frequency=1",
                cmd_open_location_csv,
                "Opens a CSV file and starts using it for location information"),
    CMD_HANDLER("add-server", "uri",
                cmd_add_server, "Adds another LwM2M Server to connect to"),
    CMD_HANDLER("trim-servers", "number",
                cmd_trim_servers,
                "Remove LwM2M Servers with specified ID and higher"),
    CMD_HANDLER("socket-count", "", cmd_socket_count,
                "Display number of sockets currently listening"),
    CMD_HANDLER("get-port", "index", cmd_get_port,
                "Display listening port number of a socket with the specified "
                "index (also supports Python-like negative indices)"),
    CMD_HANDLER("non-lwm2m-socket-count", "", cmd_non_lwm2m_socket_count,
                "Display number of sockets currently listening that are not "
                "affiliated to any LwM2M server connetion"),
    CMD_HANDLER("get-transport", "index", cmd_get_transport,
                "Display transport used by a socket with the specified index "
                "(also supports Python-like negative indices)"),
    CMD_HANDLER("enter-offline", "", cmd_enter_offline, "Enters Offline mode"),
    CMD_HANDLER("exit-offline", "", cmd_exit_offline, "Exits Offline mode"),
    CMD_HANDLER("notify", "", cmd_notify,
                "Executes anjay_notify_* on a specified path"),
    CMD_HANDLER("unregister-object", "oid", cmd_unregister_object,
                "Unregister an LwM2M Object"),
    CMD_HANDLER("download", "url target_file [psk_identity psk_key]",
                cmd_download,
                "Download a file from given CoAP URL to target_file."),
    CMD_HANDLER("set-attrs", "", cmd_set_attrs,
                "Syntax [/x [/y [/z] ] ] [pmin,pmax,lt,gt,st] - e.g. "
                "/x/y pmin=3,pmax=4"),
    CMD_HANDLER("enable-server", "ssid", cmd_enable_server,
                "Enables a server with given SSID."),
    CMD_HANDLER("get-all-connections-failed", "", cmd_all_connections_failed,
                "Returns the result of anjay_all_connections_failed()"),
    CMD_HANDLER("schedule-update-on-exit", "", cmd_schedule_update_on_exit,
                "Ensure Registration Update is scheduled for immediate "
                "execution at the point of calling anjay_delete()"),
    CMD_HANDLER("help", "", cmd_help, "Prints this message")
    // clang-format on
};
#undef CMD_HANDLER

static void print_line_with_indent(const char *line, const char *end) {
    static const int INDENT = 5;
    static const int SCREEN_WIDTH = 80;
    const int MAX_LINE_LENGTH = SCREEN_WIDTH - INDENT - 1;
    if (end - line > MAX_LINE_LENGTH) {
        const char *prev = line;
        const char *last = line;
        while (last && (last - line) <= MAX_LINE_LENGTH) {
            prev = last;
            last = strchr(last + 1, ' ');
        }
        if (prev == line) {
            prev = last;
        }
        if (prev && prev != end) {
            print_line_with_indent(line, prev);
            print_line_with_indent(prev + 1, end);
            return;
        }
    }
    for (int i = 0; i < INDENT; ++i) {
        putchar(' ');
    }
    fwrite(line, 1, (size_t) (end - line), stdout);
    putchar('\n');
}

static void print_with_indent(const char *text) {
    while (*text) {
        const char *end = strchr(text, '\n');
        if (!end) {
            end = text + strlen(text);
        }
        print_line_with_indent(text, end);
        text = end;
        if (*text) {
            ++text;
        }
    }
}

static void cmd_help(anjay_demo_t *demo, const char *args_string) {
    (void) demo;
    (void) args_string;

    puts("---");
    puts("LwM2M Demo client");
    puts("Available commands:");
    for (size_t idx = 0; idx < ARRAY_SIZE(COMMAND_HANDLERS); ++idx) {
        const struct cmd_handler_def *cmd = &COMMAND_HANDLERS[idx];
        printf("\n%s %s\n", cmd->cmd_name, cmd->help_args);
        print_with_indent(cmd->help_descr);
    }
    puts("---");
}

static void handle_command(anjay_demo_t *demo, const char *buf) {
    size_t cmdIdx = 0;
    static const size_t num_command_handlers =
            sizeof(COMMAND_HANDLERS) / sizeof(COMMAND_HANDLERS[0]);

    demo_log(INFO, "command: %s", buf);

    for (cmdIdx = 0; cmdIdx < num_command_handlers; ++cmdIdx) {
        const struct cmd_handler_def *cmd = &COMMAND_HANDLERS[cmdIdx];

        if (strncmp(buf, cmd->cmd_name, cmd->cmd_name_length) == 0) {
            cmd->handler(demo, buf + cmd->cmd_name_length);
            break;
        }
    }

    fprintf(stdout, "(DEMO)>");
    fflush(stdout);
}

void demo_command_dispatch(short revents, void *demo_) {
    anjay_demo_t *demo = (anjay_demo_t *) demo_;
    if (revents & POLLHUP) {
        demo->running = false;
    }
    if (revents & POLLIN) {
        static char buf[500] = "";

        if (fgets(buf, sizeof(buf), stdin) && buf[0]) {
            buf[strlen(buf) - 1] = 0;
            handle_command(demo, buf);
        }

        if (feof(stdin)) {
            demo->running = false;
        }
    }
}
