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

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <string.h>
#include <inttypes.h>

#include "demo_args.h"

#define DEFAULT_PSK_IDENTITY "sesame"
#define DEFAULT_PSK_KEY      "password"

static const cmdline_args_t DEFAULT_CMDLINE_ARGS = {
    .endpoint_name = "urn:dev:os:0023C7-000001",
    .connection_args = {
        .servers[0] = {
            .security_iid = ANJAY_IID_INVALID,
            .server_iid = ANJAY_IID_INVALID,
            .id = 1,
            .uri = "coap://127.0.0.1:5683"
        },
        .bootstrap_holdoff_s = 0,
        .bootstrap_timeout_s = 0,
        .lifetime = 86400,
        .binding_mode = ANJAY_BINDING_U,
        .security_mode = ANJAY_UDP_SECURITY_NOSEC,
    },
    .location_csv = NULL,
    .location_update_frequency_s = 1,
    .inbuf_size = 4000,
    .outbuf_size = 4000,
    .msg_cache_size = 0,
    .fw_updated_marker_path = "/tmp/anjay-fw-updated",
    .max_icmp_failures = 7,
    .fw_security_info = {
        .mode = (avs_net_security_mode_t) -1
    },
    .attr_storage_file = NULL,
};

static int parse_security_mode(const char *mode_string,
                               anjay_udp_security_mode_t *out_mode) {
    if (!mode_string) {
        return -1;
    }

    static const struct {
        const char *name;
        anjay_udp_security_mode_t value;
    } MODES[] = {
        { "psk",   ANJAY_UDP_SECURITY_PSK         },
        { "rpk",   ANJAY_UDP_SECURITY_RPK         },
        { "cert",  ANJAY_UDP_SECURITY_CERTIFICATE },
        { "nosec", ANJAY_UDP_SECURITY_NOSEC       },
    };

    for (size_t i = 0; i < ARRAY_SIZE(MODES); ++i) {
        if (!strcmp(mode_string, MODES[i].name)) {
            *out_mode = MODES[i].value;
            return 0;
        }
    }

    char allowed_modes[64];
    size_t offset = 0;
    for (size_t i = 0; i < ARRAY_SIZE(MODES); ++i) {
        ssize_t written = snprintf(allowed_modes + offset,
                                   sizeof(allowed_modes) - offset,
                                   " %s", MODES[i].name);
        if (written < 0
                || (size_t)written >= sizeof(allowed_modes) - offset) {
            demo_log(ERROR, "could not enumerate available security modes");
            allowed_modes[0] = '\0';
            break;
        }

        offset += (size_t)written;
    }

    demo_log(ERROR, "unrecognized security mode %s (expected one of:%s)",
             mode_string, allowed_modes);
    return -1;
}


static const char *help_arg_list(const struct option *opt) {
    switch (opt->has_arg) {
    case required_argument:
        return "ARG";
    case optional_argument:
        return "[ ARG ]";
    case no_argument:
        return "";
    default:
        return "<ERROR>";
    }
}

static void print_option_help(const struct option *opt) {
    const struct {
        char opt_val;
        const char *args;
        const char *default_value;
        const char *help;
    } HELP_INFO[] = {
        { 'a', "OBJECT_ID SHORT_SERVER_ID", NULL,
          "allow Short Server ID to instantiate Object ID." },
        { 'b', NULL, NULL, "treat first URI as Bootstrap Server." },
        { 'H', "SECONDS", "0",
          "number of seconds to wait before attempting "
          "Client Initiated Bootstrap." },
        { 'T', "SECONDS", "0",
          "number of seconds to keep the Bootstrap Server Account for after "
          "successful bootstrapping, or 0 for infinity." },
        { 'e', "URN", DEFAULT_CMDLINE_ARGS.endpoint_name,
          "endpoint name to use." },
        { 'h', NULL, NULL, "show this message and exit." },
        { 'l', "SECONDS", "86400",
          "set registration lifetime. If SECONDS <= 0, use default value and "
          "don't send lifetime in Register/Update messages." },
        { 'c', "CSV_FILE", NULL, "file to load location CSV from" },
        { 'f', "SECONDS", "1", "location update frequency in seconds" },
        { 'p', "PORT", NULL, "bind all sockets to the specified UDP port." },
        { 'i', "PSK identity (psk mode) or Public Certificate (cert mode)", NULL,
          "Both are specified as hexlified strings" },
        { 'C', "CLIENT_CERT_FILE", "$(dirname $0)/../certs/client.crt.der",
          "DER-formatted client certificate file to load. "
          "Mutually exclusive with -i" },
        { 'k', "PSK key (psk mode) or Private Certificate (cert mode)", NULL,
          "Both are specified as hexlified strings" },
        { 'K', "PRIVATE_KEY_FILE", "$(dirname $0)/../certs/client.key.der",
          "DER-formatted PKCS#8 private key complementary to the certificate "
          "specified with -C. Mutually exclusive with -k" },
        { 'q', "[BINDING_MODE=UQ]", "U", "set the Binding Mode to use." },
        { 's', "MODE", NULL, "set security mode, one of: psk rpk cert nosec." },
        { 'u', "URI", DEFAULT_CMDLINE_ARGS.connection_args.servers[0].uri,
          "server URI to use. Note: coap:// URIs require --security-mode nosec "
          "to be set. N consecutive URIs will create N servers enumerated "
          "from 1 to N." },
        { 'D', "IID", NULL, "enforce particular Security Instance IID for last "
          "configured server." },
        { 'd', "IID", NULL, "enforce particular Server Instance IID for last "
          "configured server. Ignored if last configured server is an LwM2M "
          "Bootstrap Server." },
        { 'I', "SIZE", "4000", "Nonnegative integer representing maximum "
                               "size of an incoming CoAP packet the client "
                               "should be able to handle." },
        { 'O', "SIZE", "4000", "Nonnegative integer representing maximum "
                               "size of a non-BLOCK CoAP packet the client "
                               "should be able to send." },
        { '$', "SIZE", "0", "Size, in bytes, of a buffer reserved for caching "
                            "sent responses to detect retransmissions. Setting "
                            "it to 0 disables caching mechanism." },
        { 'N', NULL, NULL,
          "Send notifications as Confirmable messages by default" },
        { 'U', "COUNT", "7", "Sets maximum number of ICMP Port/Host unreachable "
               "errors before the Server is considered unreachable" },
        { 1, "PATH", DEFAULT_CMDLINE_ARGS.fw_updated_marker_path,
          "File path to use as a marker for persisting firmware update state" },
        { 2, "CERT_FILE", NULL, "Require certificate validation against "
          "specified file when downloading firmware over encrypted channels" },
        { 3, "CERT_DIR", NULL, "Require certificate validation against files "
          "in specified path when downloading firmware over encrypted channels"
          "; note that the TLS backend may impose specific requirements for "
          "file names and formats" },
        { 4, "PSK identity", NULL, "Download firmware over encrypted channels "
          "using PSK-mode encryption with the specified identity (provided as "
          "hexlified string); must be used together with --fw-psk-key" },
        { 5, "PSK key", NULL, "Download firmware over encrypted channels using "
          "PSK-mode encryption with the specified key (provided as hexlified "
          "string); must be used together with --fw-psk-identity" },
        { 6, "PERSISTENCE_FILE", NULL,
          "File to load attribute storage data from at startup, and "
          "store it at shutdown" },
    };

    int description_offset = 25;

    fprintf(stderr, "  ");
    if (isprint(opt->val)) {
        fprintf(stderr, "-%c, ", opt->val);
        description_offset -= 4;
    }

    int chars_written;
    fprintf(stderr, "--%s %n", opt->name, &chars_written);

    for (size_t i = 0; i < ARRAY_SIZE(HELP_INFO); ++i) {
        if (opt->val == HELP_INFO[i].opt_val) {
            int padding = description_offset - chars_written;
            if (HELP_INFO[i].args) {
                int arg_length = (int) strlen(HELP_INFO[i].args);
                if (arg_length + 1 > padding) {
                    padding = arg_length + 1;
                }
            }
            fprintf(stderr, "%*s- %s", (padding > 0 ? -padding : 0),
                    HELP_INFO[i].args ? HELP_INFO[i].args : "",
                    HELP_INFO[i].help);
            if (HELP_INFO[i].default_value) {
                fprintf(stderr, " (default: %s)", HELP_INFO[i].default_value);
            }
            fprintf(stderr, "\n");
            return;
        }
    }

    fprintf(stderr, "%-15s - [NO DESCRIPTION]\n", help_arg_list(opt));
}

static int parse_i32(const char *str, int32_t *out_value) {
    long long_value;
    if (demo_parse_long(str, &long_value)
            || long_value < INT32_MIN
            || long_value > INT32_MAX) {
        demo_log(ERROR, "value out of range: %s", str);
        return -1;
    }

    *out_value = (int32_t) long_value;
    return 0;
}

static int parse_u16(const char *str, uint16_t *out_value) {
    long long_value;
    if (demo_parse_long(str, &long_value)
            || long_value < 0
            || long_value > UINT16_MAX) {
        demo_log(ERROR, "value out of range: %s", str);
        return -1;
    }

    *out_value = (uint16_t) long_value;
    return 0;
}

static int parse_hexstring(const char *str, uint8_t **out, size_t *out_size) {
    if (!str) {
        return -1;
    }

    size_t length = strlen(str);
    if (length % 2 || !length) {
        return -1;
    }
    if (*out) {
        return -1;
    }
    *out = (uint8_t *) malloc(length / 2);
    *out_size = 0;
    if (!*out) {
        return -1;
    }
    const char *curr = str;
    uint8_t *data = *out;
    while (*curr) {
        unsigned value;
        if (sscanf(curr, "%2x", &value) != 1 || (uint8_t) value != value) {
            free(*out);
            return -1;
        }
        *data++ = (uint8_t) value;
        curr += 2;
    }
    *out_size = length / 2;
    return 0;
}

static void build_getopt_string(const struct option *options,
                                char *buffer,
                                size_t buffer_size) {
    const struct option *curr_opt = options;
    char *getopt_string_ptr = buffer;

    memset(buffer, 0, buffer_size);

    while (curr_opt->val != 0) {
        assert(getopt_string_ptr - buffer < (ssize_t)buffer_size - 1);
        *getopt_string_ptr++ = (char)curr_opt->val;

        int colons = curr_opt->has_arg;
        assert(colons >= 0 && colons <= 2); // 2 colons signify optional arg
        while (colons-- > 0) {
            assert(getopt_string_ptr - buffer < (ssize_t)buffer_size - 1);
            *getopt_string_ptr++ = ':';
        }

        ++curr_opt;
    }
}

static int clone_buffer(uint8_t **out, size_t *out_size,
                        const void *src, size_t src_size) {
    *out = (uint8_t *) malloc(src_size);
    if (!*out) {
        return -1;
    }
    *out_size = src_size;
    memcpy(*out, src, src_size);
    return 0;
}

static int load_buffer_from_file(uint8_t **out, size_t *out_size,
                                 const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        return -1;
    }
    int result = -1;
    long size;
    if (fseek(f, 0, SEEK_END)) {
        goto finish;
    }
    size = ftell(f);
    if (size < 0 || (unsigned long) size > SIZE_MAX
            || fseek(f, 0, SEEK_SET)) {
        goto finish;
    }
    *out_size = (size_t) size;
    if (!(*out = (uint8_t *) malloc(*out_size))) {
        goto finish;
    }
    if (fread(*out, *out_size, 1, f) != 1) {
        free(*out);
        *out = NULL;
        goto finish;
    }
    result = 0;
finish:
    fclose(f);
    return result;
}

int demo_parse_argv(cmdline_args_t *parsed_args, int argc, char *argv[]) {
    static const char DEFAULT_CERT_FILE[] = "../certs/client.crt.der";
    static const char DEFAULT_KEY_FILE[] = "../certs/client.key.der";
    const char *last_arg0_slash = strrchr(argv[0], '/');
    size_t arg0_prefix_length =
            (size_t) (last_arg0_slash ? (last_arg0_slash - argv[0] + 1) : 0);
    bool identity_set, key_set;

    char default_cert_path[arg0_prefix_length + sizeof(DEFAULT_CERT_FILE)];
    memcpy(default_cert_path, argv[0], arg0_prefix_length);
    strcpy(default_cert_path + arg0_prefix_length, DEFAULT_CERT_FILE);

    char default_key_path[arg0_prefix_length + sizeof(DEFAULT_KEY_FILE)];
    memcpy(default_key_path, argv[0], arg0_prefix_length);
    strcpy(default_key_path + arg0_prefix_length, DEFAULT_KEY_FILE);

    const char *cert_path = default_cert_path;
    const char *key_path = default_key_path;

    *parsed_args = DEFAULT_CMDLINE_ARGS;

    const struct option options[] = {
        { "access-entry",                  required_argument, 0, 'a' },
        { "bootstrap",                     no_argument,       0, 'b' },
        { "bootstrap-holdoff",             required_argument, 0, 'H' },
        { "bootstrap-timeout",             required_argument, 0, 'T' },
        { "endpoint-name",                 required_argument, 0, 'e' },
        { "help",                          no_argument,       0, 'h' },
        { "lifetime",                      required_argument, 0, 'l' },
        { "location-csv",                  required_argument, 0, 'c' },
        { "location-update-freq-s",        required_argument, 0, 'f' },
        { "port",                          required_argument, 0, 'p' },
        { "identity",                      required_argument, 0, 'i' },
        { "client-cert-file",              required_argument, 0, 'C' },
        { "key",                           required_argument, 0, 'k' },
        { "key-file",                      required_argument, 0, 'K' },
        { "binding",                       optional_argument, 0, 'q' },
        { "security-iid",                  required_argument, 0, 'D' },
        { "security-mode",                 required_argument, 0, 's' },
        { "server-iid",                    required_argument, 0, 'd' },
        { "server-uri",                    required_argument, 0, 'u' },
        { "inbuf-size",                    required_argument, 0, 'I' },
        { "outbuf-size",                   required_argument, 0, 'O' },
        { "cache-size",                    required_argument, 0, '$' },
        { "confirmable-notifications",     no_argument,       0, 'N' },
        { "max-icmp-failures",             required_argument, 0, 'U' },
        { "fw-updated-marker-path",        required_argument, 0, 1 },
        { "fw-cert-file",                  required_argument, 0, 2 },
        { "fw-cert-path",                  required_argument, 0, 3 },
        { "fw-psk-identity",               required_argument, 0, 4 },
        { "fw-psk-key",                    required_argument, 0, 5 },
        { "attribute-storage-persistence-file", required_argument, 0, 6 },
        { 0, 0, 0, 0 }
    };
    int num_servers = 0;

    char getopt_str[3 * ARRAY_SIZE(options)];
    build_getopt_string(options, getopt_str, sizeof(getopt_str));

    while (true) {
        int option_index = 0;

        switch (getopt_long(argc, argv, getopt_str, options, &option_index)) {
        case '?':
            demo_log(ERROR, "unrecognized cmdline argument: %s",
                     argv[option_index]);
            goto error;
        case -1:
            goto finish;
        case 'a': {
            /* optind is the index of the next argument to be processed, which
               means that argv[optind-1] is the current one. We shall fail if
               more than one free argument is provided */
            if (*argv[optind-1] == '-'
                    || optind == argc
                    || *argv[optind] == '-'
                    || (optind + 1 < argc && *argv[optind + 1] != '-')) {
                demo_log(ERROR, "invalid pair OID SSID");
                goto error;
            }

            AVS_LIST(access_entry_t) entry =
                AVS_LIST_NEW_ELEMENT(access_entry_t);
            if (!entry) {
                goto error;
            }

            // We ignore the fact, that someone passes bad input, and silently
            // truncate it to zero.
            long oid = strtol(argv[optind-1], NULL, 10);
            long ssid = strtol(argv[optind], NULL, 10);
            entry->oid = (anjay_oid_t) oid;
            entry->ssid = (anjay_ssid_t) ssid;
            AVS_LIST_INSERT(&parsed_args->access_entries, entry);
            break;
        }
        case 'b':
            parsed_args->connection_args.servers[0].is_bootstrap = true;
            break;
        case 'H':
            if (parse_i32(optarg,
                          &parsed_args->connection_args.bootstrap_holdoff_s)) {
                goto error;
            }
            break;
        case 'T':
            if (parse_i32(optarg,
                          &parsed_args->connection_args.bootstrap_timeout_s)) {
                goto error;
            }
            break;
        case 'e':
            parsed_args->endpoint_name = optarg;
            break;
        case 'h':
            fprintf(stderr, "Available options:\n");
            for (size_t i = 0; options[i].name || options[i].val; ++i) {
                print_option_help(&options[i]);
            }
            goto error;
        case 'l':
            if (parse_i32(optarg, &parsed_args->connection_args.lifetime)) {
                goto error;
            }
            break;
        case 'c':
            parsed_args->location_csv = optarg;
            break;
        case 'f':
            {
                long freq;
                if (demo_parse_long(optarg, &freq)
                        || freq <= 0
                        || freq > INT32_MAX) {
                    demo_log(ERROR, "invalid location update frequency: %s",
                             optarg);
                    goto error;
                }

                parsed_args->location_update_frequency_s = (time_t)freq;
            }
            break;
        case 'p':
            {
                long port;
                if (demo_parse_long(optarg, &port)
                        || port <= 0
                        || port > UINT16_MAX) {
                    demo_log(ERROR, "invalid UDP port number: %s", optarg);
                    goto error;
                }

                parsed_args->udp_listen_port = (uint16_t)port;
            }
            break;
        case 'i':
            if (parse_hexstring(optarg, &parsed_args->connection_args
                                                 .public_cert_or_psk_identity,
                                &parsed_args->connection_args
                                         .public_cert_or_psk_identity_size)) {
                demo_log(ERROR, "Invalid identity");
                goto error;
            }
            break;
        case 'C':
            cert_path = optarg;
            break;
        case 'k':
            if (parse_hexstring(
                        optarg,
                        &parsed_args->connection_args.private_cert_or_psk_key,
                        &parsed_args->connection_args
                                 .private_cert_or_psk_key_size)) {
                demo_log(ERROR, "Invalid key");
                goto error;
            }
            break;
        case 'K':
            key_path = optarg;
            break;
        case 'q':
            {
                anjay_binding_mode_t binding_mode = ANJAY_BINDING_NONE;
                if (optarg && *optarg) {
                    binding_mode = anjay_binding_mode_from_str(optarg);
                }
                // default to UQ if optional argument is not present
                // for compatibility with legacy -q being --queue
                if (binding_mode == ANJAY_BINDING_NONE) {
                    binding_mode = ANJAY_BINDING_UQ;
                }
                parsed_args->connection_args.binding_mode = binding_mode;
                break;
            }
        case 'D': {
                int idx = num_servers == 0 ? 0 : num_servers - 1;
                if (parse_u16(optarg,
                              &parsed_args->connection_args.servers[idx]
                                                           .security_iid)) {
                    goto error;
                }
            }
            break;
        case 's':
            if (parse_security_mode(
                    optarg, &parsed_args->connection_args.security_mode)) {
                goto error;
            }
            break;
        case 'd': {
                int idx = num_servers == 0 ? 0 : num_servers - 1;
                if (parse_u16(optarg,
                              &parsed_args->connection_args.servers[idx]
                                                           .server_iid)) {
                    goto error;
                }
            }
            break;
        case 'u': {
                assert(num_servers < MAX_SERVERS && "Too many servers");
                server_entry_t *entry =
                        &parsed_args->connection_args.servers[num_servers++];
                entry->uri = optarg;
                entry->security_iid = ANJAY_IID_INVALID;
                entry->server_iid = ANJAY_IID_INVALID;
            }
            break;
        case 'I':
            if (parse_i32(optarg, &parsed_args->inbuf_size)
                    || parsed_args->inbuf_size <= 0) {
                goto error;
            }
            break;
        case 'O':
            if (parse_i32(optarg, &parsed_args->outbuf_size)
                    || parsed_args->outbuf_size <= 0) {
                goto error;
            }
            break;
        case '$':
            if (parse_i32(optarg, &parsed_args->msg_cache_size)
                    || parsed_args->msg_cache_size < 0) {
                goto error;
            }
            break;
        case 'N':
            parsed_args->confirmable_notifications = true;
            break;
        case 'U': {
            int32_t max_icmp_failures;
            if (parse_i32(optarg, &max_icmp_failures)
                    || max_icmp_failures < 0) {
                goto error;
            }
            parsed_args->max_icmp_failures = (uint32_t)max_icmp_failures;
            break;
        }
        case 1:
            parsed_args->fw_updated_marker_path = optarg;
            break;
        case 2:
            if (parsed_args->fw_security_info.mode
                    != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto error;
            }
            parsed_args->fw_security_info =
                    avs_net_security_info_from_certificates(
                            (avs_net_certificate_info_t) {
                                .server_cert_validation = true,
                                .trusted_certs =
                                        avs_net_trusted_cert_info_from_file(optarg)
                            });
            break;
        case 3:
            if (parsed_args->fw_security_info.mode
                    != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto error;
            }
            parsed_args->fw_security_info =
                    avs_net_security_info_from_certificates(
                            (avs_net_certificate_info_t) {
                                .server_cert_validation = true,
                                .trusted_certs =
                                        avs_net_trusted_cert_info_from_path(optarg)
                            });
            break;
        case 4:
            if (parsed_args->fw_security_info.mode != AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.mode
                            != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto error;
            }
            if (parsed_args->fw_security_info.mode == AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.data.psk.identity) {
                demo_log(ERROR, "--fw-psk-identity specified more than once");
                goto error;
            }
            if (parse_hexstring(
                    optarg,
                    (uint8_t **) (intptr_t)
                            &parsed_args->fw_security_info.data.psk.identity,
                    &parsed_args->fw_security_info.data.psk.identity_size)) {
                demo_log(ERROR, "Invalid PSK identity for firmware upgrade");
                goto error;
            }
            parsed_args->fw_security_info.mode = AVS_NET_SECURITY_PSK;
            break;
        case 5:
            if (parsed_args->fw_security_info.mode != AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.mode
                            != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto error;
            }
            if (parsed_args->fw_security_info.mode == AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.data.psk.psk) {
                demo_log(ERROR, "--fw-psk-key specified more than once");
                goto error;
            }
            if (parse_hexstring(
                    optarg,
                    (uint8_t **) (intptr_t)
                            &parsed_args->fw_security_info.data.psk.psk,
                    &parsed_args->fw_security_info.data.psk.psk_size)) {
                demo_log(ERROR, "Invalid pre-shared key for firmware upgrade");
                goto error;
            }
            parsed_args->fw_security_info.mode = AVS_NET_SECURITY_PSK;
            break;
        case 6:
            parsed_args->attr_storage_file = optarg;
            break;
        case 0:
            goto finish;
        }
    }
finish:
    for (int i = 0; i < AVS_MAX(num_servers, 1); ++i) {
        server_entry_t *entry = &parsed_args->connection_args.servers[i];
        entry->id = (anjay_ssid_t) (i + 1);
        if (entry->security_iid == ANJAY_IID_INVALID) {
            entry->security_iid = (anjay_iid_t) entry->id;
        }
        if (entry->server_iid == ANJAY_IID_INVALID) {
            entry->server_iid = (anjay_iid_t) entry->id;
        }
    }
    identity_set =
            !!parsed_args->connection_args.public_cert_or_psk_identity_size;
    key_set = !!parsed_args->connection_args.private_cert_or_psk_key_size;
    if ((identity_set && (cert_path != default_cert_path))
            || (key_set && (key_path != default_key_path))) {
        demo_log(ERROR, "Certificate information cannot be loaded both from "
                        "file and immediate hex data at the same time");
        goto error;
    }
    if (parsed_args->connection_args.security_mode == ANJAY_UDP_SECURITY_PSK) {
        if (!identity_set
            && clone_buffer(&parsed_args->connection_args
                                     .public_cert_or_psk_identity,
                            &parsed_args->connection_args
                                     .public_cert_or_psk_identity_size,
                            DEFAULT_PSK_IDENTITY,
                            sizeof(DEFAULT_PSK_IDENTITY) - 1)) {
            goto error;
        }
        if (!key_set
            && clone_buffer(
                       &parsed_args->connection_args.private_cert_or_psk_key,
                       &parsed_args->connection_args
                                .private_cert_or_psk_key_size,
                       DEFAULT_PSK_KEY, sizeof(DEFAULT_PSK_KEY) - 1)) {
            goto error;
        }
    } else if (parsed_args->connection_args.security_mode
            == ANJAY_UDP_SECURITY_CERTIFICATE) {
        if (identity_set ^ key_set) {
            demo_log(ERROR, "Setting public cert but not private cert (and "
                            "other way around) makes little sense");
            goto error;
        } else if (!identity_set) {
            if (load_buffer_from_file(&parsed_args->connection_args
                                              .public_cert_or_psk_identity,
                                      &parsed_args->connection_args
                                              .public_cert_or_psk_identity_size,
                                      cert_path)) {
                demo_log(ERROR, "Could not load certificate from %s",
                         cert_path);
                goto error;
            }
            if (load_buffer_from_file(&parsed_args->connection_args
                                              .private_cert_or_psk_key,
                                      &parsed_args->connection_args
                                              .private_cert_or_psk_key_size,
                                      key_path)) {
                demo_log(ERROR, "Could not load private key from %s",
                         key_path);
                goto error;
            }
        }
    }
    if (parsed_args->fw_security_info.mode == AVS_NET_SECURITY_PSK
            && (!parsed_args->fw_security_info.data.psk.identity
                    || !parsed_args->fw_security_info.data.psk.psk)) {
        demo_log(ERROR, "Both identity and key must be provided when using PSK "
                        "for firmware upgrade security");
        goto error;
    }
    return 0;
error:
    AVS_LIST_CLEAR(&parsed_args->access_entries);
    return -1;
}
