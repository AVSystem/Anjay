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

#include <assert.h>
#include <getopt.h>
#include <string.h>

#include "demo_args.h"

#ifdef DEMO_SSL_KEY_FILE
#include DEMO_SSL_KEY_FILE
#endif

#define DEFAULT_PSK_IDENTITY "sesame"
#define DEFAULT_PSK_KEY      "password"

static const cmdline_args_t DEFAULT_CMDLINE_ARGS = {
    .endpoint_name = "urn:dev:os:0023C7-000001",
    .connection_args = {
        .servers[0] = {
            .id = 1,
            .uri = "coap://127.0.0.1:5683"
        },
        .bootstrap_holdoff_s = 0,
        .bootstrap_timeout_s = 0,
        .lifetime = 86400,
        .binding_mode = ANJAY_BINDING_U,
        .security_mode = ANJAY_UDP_SECURITY_NOSEC
    },
    .location_csv = NULL,
    .location_update_frequency_s = 1,
    .inbuf_size = 4000,
    .outbuf_size = 4000,
    .cleanup_fw_on_upgrade = true
};

static int parse_security_mode(const char *mode_string,
                               anjay_udp_security_mode_t *out_mode) {
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
        { 'C', NULL, NULL, "Do not remove firmware image after successful upgrade. "
                           "By default firmware is being removed." },
        { 'f', "SECONDS", "1", "location update frequency in seconds" },
        { 'p', "PORT", NULL, "bind all sockets to the specified UDP port." },
        { 'i', "PSK identity (psk mode) or Public Certificate (cert mode)", NULL,
          "Both are specified as hexlified strings" },
        { 'k', "PSK key (psk mode) or Private Certificate (cert mode)", NULL,
          "Both are specified as hexlified strings" },
        { 'q', "[BINDING_MODE=UQ]", "U", "set the Binding Mode to use." },
        { 's', "MODE", NULL, "set security mode, one of: psk rpk cert nosec." },
        { 'u', "URI", DEFAULT_CMDLINE_ARGS.connection_args.servers[0].uri,
          "server URI to use. Note: coap:// URIs require --security-mode nosec "
          "to be set. N consecutive URIs will create N servers enumerated "
          "from 1 to N." },
        { 'I', "SIZE", "4000", "Nonnegative integer representing maximum "
                               "size of an incoming CoAP packet the client "
                               "should be able to handle." },
        { 'O', "SIZE", "4000", "Nonnegative integer representing maximum "
                               "size of a non-BLOCK CoAP packet the client "
                               "should be able to send." },
    };

    fprintf(stderr, "  ");
    if (opt->val) {
        fprintf(stderr, "-%c, ", opt->val);
    }

    static const int DESCRIPTION_OFFSET = 20;
    int chars_written;
    fprintf(stderr, "--%s %n", opt->name, &chars_written);

    for (size_t i = 0; i < ARRAY_SIZE(HELP_INFO); ++i) {
        if (opt->val == HELP_INFO[i].opt_val) {
            int padding = DESCRIPTION_OFFSET - chars_written;
            fprintf(stderr, "%*s - %s", -padding,
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
    if (demo_parse_long(optarg, &long_value)
            || long_value < INT32_MIN
            || long_value > INT32_MAX) {
        demo_log(ERROR, "value out of range: %s", str);
        return -1;
    }

    *out_value = (int32_t) long_value;
    return 0;
}

static int parse_hexstring(const char *str, uint8_t **out, size_t *out_size) {
    size_t length = strlen(str);
    if (length % 2 || !length) {
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

static int clone_buffer(uint8_t **out, size_t *out_size, const void *src, size_t src_size) {
    *out = (uint8_t *) malloc(src_size);
    if (!*out) {
        return -1;
    }
    *out_size = src_size;
    memcpy(*out, src, src_size);
    return 0;
}

int demo_parse_argv(cmdline_args_t *parsed_args, int argc, char *argv[]) {
    *parsed_args = DEFAULT_CMDLINE_ARGS;

    static const struct option options[] = {
        { "access-entry",                required_argument, 0, 'a' },
        { "bootstrap",                   no_argument,       0, 'b' },
        { "bootstrap-holdoff",           required_argument, 0, 'H' },
        { "bootstrap-timeout",           required_argument, 0, 'T' },
        { "endpoint-name",               required_argument, 0, 'e' },
        { "help",                        no_argument,       0, 'h' },
        { "lifetime",                    required_argument, 0, 'l' },
        { "location-csv",                required_argument, 0, 'c' },
        { "location-update-freq-s",      required_argument, 0, 'f' },
        { "port",                        required_argument, 0, 'p' },
        { "identity",                    required_argument, 0, 'i' },
        { "key",                         required_argument, 0, 'k' },
        { "binding",                     optional_argument, 0, 'q' },
        { "security-mode",               required_argument, 0, 's' },
        { "server-uri",                  required_argument, 0, 'u' },
        { "inbuf-size",                  required_argument, 0, 'I' },
        { "outbuf-size",                 required_argument, 0, 'O' },
        { "dont-cleanup-fw-on-upgrade",  no_argument,       0, 'C' },
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
            for (size_t i = 0; options[i].val != 0; ++i) {
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
        case 'C':
            parsed_args->cleanup_fw_on_upgrade = false;
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
        case 's':
            if (parse_security_mode(
                    optarg, &parsed_args->connection_args.security_mode)) {
                goto error;
            }
            break;
        case 'u':
            assert(num_servers < MAX_SERVERS && "Too many servers");
            parsed_args->connection_args.servers[num_servers++].uri = optarg;
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
        case 0:
            goto finish;
        }
    }
finish:
    for (int i = 1; i < num_servers; ++i) {
        // update IDs
        parsed_args->connection_args.servers[i].id = (anjay_ssid_t) (i + 1);
    }
    bool identity_set =
            !!parsed_args->connection_args.public_cert_or_psk_identity_size;
    bool key_set = !!parsed_args->connection_args.private_cert_or_psk_key_size;
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
#ifdef DEMO_SSL_KEY_FILE
            if (clone_buffer(&parsed_args->connection_args
                                      .public_cert_or_psk_identity,
                             &parsed_args->connection_args
                                      .public_cert_or_psk_identity_size,
                             ANJAY_DEMO_CLIENT_X509_CERTIFICATE,
                             sizeof(ANJAY_DEMO_CLIENT_X509_CERTIFICATE) - 1)
                || clone_buffer(&parsed_args->connection_args
                                         .private_cert_or_psk_key,
                                &parsed_args->connection_args
                                         .private_cert_or_psk_key_size,
                                ANJAY_DEMO_CLIENT_PKCS8_PRIVATE_KEY,
                                sizeof(ANJAY_DEMO_CLIENT_PKCS8_PRIVATE_KEY) - 1))
#endif
            {
                goto error;
            }
        }
    }
    return 0;
error:
    AVS_LIST_CLEAR(&parsed_args->access_entries);
    return -1;
}

