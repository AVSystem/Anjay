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

#include "demo_args.h"
#include "demo.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <string.h>

#include <avsystem/commons/avs_memory.h>

#define DEFAULT_PSK_IDENTITY "sesame"
#define DEFAULT_PSK_KEY "password"

static const cmdline_args_t DEFAULT_CMDLINE_ARGS = {
    .endpoint_name = "urn:dev:os:0023C7-000001",
    .connection_args = {
        .servers[0] = {
            .security_iid = ANJAY_ID_INVALID,
            .server_iid = ANJAY_ID_INVALID,
            .id = 1,
            .uri = "coap://127.0.0.1:5683",
            .binding_mode = "U",
        },
        .bootstrap_holdoff_s = 0,
        .bootstrap_timeout_s = 0,
        .lifetime = 86400,
        .security_mode = ANJAY_SECURITY_NOSEC
    },
    .location_csv = NULL,
    .location_update_frequency_s = 1,
    .inbuf_size = 4000,
    .outbuf_size = 4000,
    .msg_cache_size = 0,
    .fw_updated_marker_path = "/tmp/anjay-fw-updated",
    .fw_security_info = {
        .mode = (avs_net_security_mode_t) -1
    },
    .attr_storage_file = NULL,
    .disable_legacy_server_initiated_bootstrap = false,
    .tx_params = ANJAY_COAP_DEFAULT_UDP_TX_PARAMS,
    .dtls_hs_tx_params = ANJAY_DTLS_DEFAULT_UDP_HS_TX_PARAMS,
    .fwu_tx_params_modified = false,
    .fwu_tx_params = ANJAY_COAP_DEFAULT_UDP_TX_PARAMS,
    .prefer_hierarchical_formats = false
};

static int parse_security_mode(const char *mode_string,
                               anjay_security_mode_t *out_mode) {
    if (!mode_string) {
        return -1;
    }

    static const struct {
        const char *name;
        anjay_security_mode_t value;
    } MODES[] = {
        // clang-format off
        { "psk",   ANJAY_SECURITY_PSK         },
        { "rpk",   ANJAY_SECURITY_RPK         },
        { "cert",  ANJAY_SECURITY_CERTIFICATE },
        { "nosec", ANJAY_SECURITY_NOSEC       },
        // clang-format on
    };

    for (size_t i = 0; i < AVS_ARRAY_SIZE(MODES); ++i) {
        if (!strcmp(mode_string, MODES[i].name)) {
            *out_mode = MODES[i].value;
            return 0;
        }
    }

    char allowed_modes[64];
    size_t offset = 0;
    for (size_t i = 0; i < AVS_ARRAY_SIZE(MODES); ++i) {
        int written =
                snprintf(allowed_modes + offset, sizeof(allowed_modes) - offset,
                         " %s", MODES[i].name);
        if (written < 0 || (size_t) written >= sizeof(allowed_modes) - offset) {
            demo_log(ERROR, "could not enumerate available security modes");
            allowed_modes[0] = '\0';
            break;
        }

        offset += (size_t) written;
    }

    demo_log(ERROR, "unrecognized security mode %s (expected one of:%s)",
             mode_string, allowed_modes);
    return -1;
}

static const char *help_arg_list(const struct option *opt) {
    switch (opt->has_arg) {
    case required_argument:
        return " ARG";
    case optional_argument:
        return "[=ARG]";
    case no_argument:
        return " ";
    default:
        return " <ERROR>";
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
        { 'b', "client-initiated-only", NULL,
          "treat first URI as Bootstrap Server. If the optional "
          "\"client-initiated-only\" option is specified, the legacy LwM2M "
          "1.0-style Server-Initiated bootstrap mode is not available." },
        { 'H', "SECONDS", "0",
          "number of seconds to wait before attempting Client Initiated "
          "Bootstrap." },
        { 'T', "SECONDS", "0",
          "number of seconds to keep the Bootstrap Server Account for after "
          "successful bootstrapping, or 0 for infinity." },
        { 'e', "URN", DEFAULT_CMDLINE_ARGS.endpoint_name,
          "endpoint name to use." },
        { 'h', NULL, NULL, "show this message and exit." },
#ifndef _WIN32
        { 't', NULL, NULL,
          "disables standard input. Useful for running the client as a "
          "daemon." },
#endif // _WIN32
        { 'l', "SECONDS", "86400",
          "set registration lifetime. If SECONDS <= 0, use default value and "
          "don't send lifetime in Register/Update messages." },
        { 'L', "MAX_NOTIFICATIONS", "0",
          "set limit of queued notifications in queue/offline mode. 0: "
          "unlimited; >0: keep that much newest ones" },
        { 'c', "CSV_FILE", NULL, "file to load location CSV from" },
        { 'f', "SECONDS", "1", "location update frequency in seconds" },
        { 'p', "PORT", NULL, "bind all sockets to the specified UDP port." },
        { 'i', "PSK identity (psk mode) or Public Certificate (cert mode)",
          NULL, "Both are specified as hexlified strings" },
        { 'C', "CLIENT_CERT_FILE", "$(dirname $0)/../certs/client.crt.der",
          "DER-formatted client certificate file to load. Mutually exclusive "
          "with -i" },
        { 'k', "PSK key (psk mode) or Private Certificate (cert mode)", NULL,
          "Both are specified as hexlified strings" },
        { 'K', "PRIVATE_KEY_FILE", "$(dirname $0)/../certs/client.key.der",
          "DER-formatted PKCS#8 private key complementary to the certificate "
          "specified with -C. Mutually exclusive with -k" },
        { 'P', "SERVER_PUBLIC_KEY_FILE",
          "$(dirname $0)/../certs/server.crt.der",
          "DER-formatted server public key file to load." },
        { 'q', "BINDING_MODE=UQ", "U",
          "set the Binding Mode to use for the currently configured server." },
        { 's', "MODE", NULL, "set security mode, one of: psk rpk cert nosec." },
        { 'u', "URI", DEFAULT_CMDLINE_ARGS.connection_args.servers[0].uri,
          "server URI to use. Note: coap:// URIs require --security-mode nosec "
          "to be set. N consecutive URIs will create N servers enumerated "
          "from 1 to N." },
        { 'D', "IID", NULL,
          "enforce particular Security Instance IID for last configured "
          "server." },
        { 'd', "IID", NULL,
          "enforce particular Server Instance IID for last configured server. "
          "Ignored if last configured server is an LwM2M Bootstrap Server." },
        { 'I', "SIZE", "4000",
          "Nonnegative integer representing maximum size of an incoming CoAP "
          "packet the client should be able to handle." },
        { 'O', "SIZE", "4000",
          "Nonnegative integer representing maximum size of a non-BLOCK CoAP "
          "packet the client should be able to send." },
        { '$', "SIZE", "0",
          "Size, in bytes, of a buffer reserved for caching sent responses to "
          "detect retransmissions. Setting it to 0 disables caching "
          "mechanism." },
        { 'N', NULL, NULL,
          "Send notifications as Confirmable messages by default" },
        { 'r', "RESULT", NULL,
          "If specified and nonzero, initializes the Firmware Update object in "
          "UPDATING state, and sets the result to given value after a short "
          "while" },
        { 1, "PATH", DEFAULT_CMDLINE_ARGS.fw_updated_marker_path,
          "File path to use as a marker for persisting firmware update state" },
        { 2, "CERT_FILE", NULL,
          "Require certificate validation against specified file when "
          "downloading firmware over encrypted channels" },
        { 3, "CERT_DIR", NULL,
          "Require certificate validation against files in specified path when "
          "downloading firmware over encrypted channels; note that the TLS "
          "backend may impose specific requirements for file names and "
          "formats" },
        { 4, "PSK identity", NULL,
          "Download firmware over encrypted channels using PSK-mode encryption "
          "with the specified identity (provided as hexlified string); must be "
          "used together with --fw-psk-key" },
        { 5, "PSK key", NULL,
          "Download firmware over encrypted channels using PSK-mode encryption "
          "with the specified key (provided as hexlified string); must be used "
          "together with --fw-psk-identity" },
        { 6, "PERSISTENCE_FILE", NULL,
          "File to load attribute storage data from at startup, and "
          "store it at shutdown" },
        { 12, "ACK_RANDOM_FACTOR", "1.5",
          "Configures ACK_RANDOM_FACTOR (defined in RFC7252)" },
        { 13, "ACK_TIMEOUT", "2.0",
          "Configures ACK_TIMEOUT (defined in RFC7252) in seconds" },
        { 14, "MAX_RETRANSMIT", "4",
          "Configures MAX_RETRANSMIT (defined in RFC7252)" },
        { 15, "DTLS_HS_RETRY_WAIT_MIN", "1",
          "Configures minimum period of time to wait before sending first "
          "DTLS HS retransmission" },
        { 16, "DTLS_HS_RETRY_WAIT_MAX", "60",
          "Configures maximum period of time to wait (after last "
          "retransmission) before giving up on handshake completely" },
        { 17, "ACK_RANDOM_FACTOR", "1.5",
          "Configures ACK_RANDOM_FACTOR (defined in RFC7252) for firmware "
          "update" },
        { 18, "ACK_TIMEOUT", "2.0",
          "Configures ACK_TIMEOUT (defined in RFC7252) in seconds for firmware "
          "update" },
        { 19, "MAX_RETRANSMIT", "4",
          "Configures MAX_RETRANSMIT (defined in RFC7252) for firmware "
          "update" },
        { 20, NULL, NULL,
          "Sets the library to use hierarchical content formats by default for "
          "all responses." },
        { 22, NULL, NULL, "Enables DTLS connection_id extension." },
        { 23, "CIPHERSUITE[,CIPHERSUITE...]", "TLS library defaults",
          "Sets the ciphersuites to be used by default for (D)TLS "
          "connections." },
    };

    int description_offset = 25;

    fprintf(stderr, "  ");
    if (isprint(opt->val)) {
        fprintf(stderr, "-%c, ", opt->val);
        description_offset -= 4;
    }

    int chars_written = 0;
    fprintf(stderr, "--%s%n", opt->name, &chars_written);

    int padding = description_offset - chars_written - 1;
    for (size_t i = 0; i < AVS_ARRAY_SIZE(HELP_INFO); ++i) {
        if (opt->val == HELP_INFO[i].opt_val) {
            const char *args = HELP_INFO[i].args ? HELP_INFO[i].args : "";
            const char *arg_prefix = "";
            const char *arg_suffix = "";
            if (opt->has_arg == required_argument) {
                arg_prefix = " ";
            } else if (opt->has_arg == optional_argument) {
                arg_prefix = "[=";
                arg_suffix = "]";
            }
            padding -= (int) (strlen(arg_prefix) + strlen(args));
            fprintf(stderr, "%s%s%*s - %s", arg_prefix, args,
                    padding > 0 ? -padding : 0, arg_suffix, HELP_INFO[i].help);
            if (HELP_INFO[i].default_value) {
                fprintf(stderr, " (default: %s)", HELP_INFO[i].default_value);
            }
            fprintf(stderr, "\n");
            return;
        }
    }

    fprintf(stderr, "%*s - [NO DESCRIPTION]\n", padding > 0 ? -padding : 0,
            help_arg_list(opt));
}

static int parse_i32(const char *str, int32_t *out_value) {
    long long_value;
    if (demo_parse_long(str, &long_value) || long_value < INT32_MIN
            || long_value > INT32_MAX) {
        demo_log(ERROR,
                 "value out of range: expected 32-bit signed value, got %s",
                 str);
        return -1;
    }

    *out_value = (int32_t) long_value;
    return 0;
}

static int parse_u32(const char *str, uint32_t *out_value) {
    long long_value;
    if (demo_parse_long(str, &long_value) || long_value < 0
            || long_value > UINT32_MAX) {
        demo_log(ERROR,
                 "value out of range: expected 32-bit unsigned value, got %s",
                 str);
        return -1;
    }

    *out_value = (uint32_t) long_value;
    return 0;
}

static int parse_u16(const char *str, uint16_t *out_value) {
    long long_value;
    if (demo_parse_long(str, &long_value) || long_value < 0
            || long_value > UINT16_MAX) {
        demo_log(ERROR,
                 "value out of range: expected 16-bit unsigned value, got %s",
                 str);
        return -1;
    }

    *out_value = (uint16_t) long_value;
    return 0;
}

static int parse_size(const char *str, size_t *out_value) {
    long long_value;
    if (demo_parse_long(str, &long_value) || long_value < 0
#if SIZE_MAX < LONG_MAX
            || long_value > SIZE_MAX
#endif
    ) {
        demo_log(ERROR,
                 "value out of range: expected %d-bit unsigned value, got %s",
                 (int) (CHAR_BIT * sizeof(size_t)), str);
        return -1;
    }

    *out_value = (size_t) long_value;
    return 0;
}

static int parse_double(const char *str, double *out_value) {
    assert(str);
    errno = 0;
    char *endptr = NULL;
    *out_value = strtod(str, &endptr);
    if (!*str || isspace((unsigned char) *str) || errno || !endptr || *endptr) {
        return -1;
    }
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
    *out = (uint8_t *) avs_malloc(length / 2);
    *out_size = 0;
    if (!*out) {
        return -1;
    }
    const char *curr = str;
    uint8_t *data = *out;
    while (*curr) {
        unsigned value;
        if (sscanf(curr, "%2x", &value) != 1 || (uint8_t) value != value) {
            avs_free(*out);
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
        assert(getopt_string_ptr - buffer < (ptrdiff_t) buffer_size - 1);
        *getopt_string_ptr++ = (char) curr_opt->val;

        int colons = curr_opt->has_arg;
        assert(colons >= 0 && colons <= 2); // 2 colons signify optional arg
        while (colons-- > 0) {
            assert(getopt_string_ptr - buffer < (ptrdiff_t) buffer_size - 1);
            *getopt_string_ptr++ = ':';
        }

        ++curr_opt;
    }
}

static int clone_buffer(uint8_t **out,
                        size_t *out_size,
                        const void *src,
                        size_t src_size) {
    *out = (uint8_t *) avs_malloc(src_size);
    if (!*out) {
        return -1;
    }
    *out_size = src_size;
    memcpy(*out, src, src_size);
    return 0;
}

static int
load_buffer_from_file(uint8_t **out, size_t *out_size, const char *filename) {
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
    if (size < 0 || (unsigned long) size > SIZE_MAX || fseek(f, 0, SEEK_SET)) {
        goto finish;
    }
    if (!(*out_size = (size_t) size)) {
        *out = NULL;
    } else {
        if (!(*out = (uint8_t *) avs_malloc(*out_size))) {
            goto finish;
        }
        if (fread(*out, *out_size, 1, f) != 1) {
            avs_free(*out);
            *out = NULL;
            goto finish;
        }
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
    int num_servers = 0;

    const struct option options[] = {
        // clang-format off
        { "access-entry",                  required_argument, 0, 'a' },
        { "bootstrap",                     optional_argument, 0, 'b' },
        { "bootstrap-holdoff",             required_argument, 0, 'H' },
        { "bootstrap-timeout",             required_argument, 0, 'T' },
        { "endpoint-name",                 required_argument, 0, 'e' },
        { "help",                          no_argument,       0, 'h' },
#ifndef _WIN32
        { "disable-stdin",                 no_argument,       0, 't' },
#endif // _WIN32
        { "lifetime",                      required_argument, 0, 'l' },
        { "stored-notification-limit",     required_argument, 0, 'L' },
        { "location-csv",                  required_argument, 0, 'c' },
        { "location-update-freq-s",        required_argument, 0, 'f' },
        { "port",                          required_argument, 0, 'p' },
        { "identity",                      required_argument, 0, 'i' },
        { "client-cert-file",              required_argument, 0, 'C' },
        { "key",                           required_argument, 0, 'k' },
        { "key-file",                      required_argument, 0, 'K' },
        { "server-public-key-file",        required_argument, 0, 'P' },
        { "binding",                       required_argument, 0, 'q' },
        { "security-iid",                  required_argument, 0, 'D' },
        { "security-mode",                 required_argument, 0, 's' },
        { "server-iid",                    required_argument, 0, 'd' },
        { "server-uri",                    required_argument, 0, 'u' },
        { "inbuf-size",                    required_argument, 0, 'I' },
        { "outbuf-size",                   required_argument, 0, 'O' },
        { "cache-size",                    required_argument, 0, '$' },
        { "confirmable-notifications",     no_argument,       0, 'N' },
        { "delayed-upgrade-result",        required_argument, 0, 'r' },
        { "fw-updated-marker-path",        required_argument, 0, 1 },
        { "fw-cert-file",                  required_argument, 0, 2 },
        { "fw-cert-path",                  required_argument, 0, 3 },
        { "fw-psk-identity",               required_argument, 0, 4 },
        { "fw-psk-key",                    required_argument, 0, 5 },
        { "attribute-storage-persistence-file", required_argument, 0, 6 },
        { "ack-random-factor",             required_argument, 0, 12 },
        { "ack-timeout",                   required_argument, 0, 13 },
        { "max-retransmit",                required_argument, 0, 14 },
        { "dtls-hs-retry-wait-min",        required_argument, 0, 15 },
        { "dtls-hs-retry-wait-max",        required_argument, 0, 16 },
        { "fwu-ack-random-factor",         required_argument, 0, 17 },
        { "fwu-ack-timeout",               required_argument, 0, 18 },
        { "fwu-max-retransmit",            required_argument, 0, 19 },
        { "prefer-hierarchical-formats",   no_argument,       0, 20 },
        { "use-connection-id",             no_argument,       0, 22 },
        { "ciphersuites",                  required_argument, 0, 23 },
        { 0, 0, 0, 0 }
        // clang-format on
    };

    int retval = -1;

    *parsed_args = DEFAULT_CMDLINE_ARGS;

    char *default_cert_path =
            (char *) avs_malloc(arg0_prefix_length + sizeof(DEFAULT_CERT_FILE));
    char *default_key_path =
            (char *) avs_malloc(arg0_prefix_length + sizeof(DEFAULT_KEY_FILE));
    const char *cert_path = default_cert_path;
    const char *key_path = default_key_path;
    const char *server_public_key_path = NULL;

    if (!default_cert_path || !default_key_path) {
        demo_log(ERROR, "Out of memory");
        goto finish;
    }

    memcpy(default_cert_path, argv[0], arg0_prefix_length);
    strcpy(default_cert_path + arg0_prefix_length, DEFAULT_CERT_FILE);

    memcpy(default_key_path, argv[0], arg0_prefix_length);
    strcpy(default_key_path + arg0_prefix_length, DEFAULT_KEY_FILE);

    char getopt_str[3 * AVS_ARRAY_SIZE(options)];
    build_getopt_string(options, getopt_str, sizeof(getopt_str));

    while (true) {
        int option_index = 0;

        switch (getopt_long(argc, argv, getopt_str, options, &option_index)) {
        case '?':
            demo_log(ERROR, "unrecognized cmdline argument: %s",
                     argv[option_index]);
            goto finish;
        case -1:
            goto process;
        case 'a': {
            /* optind is the index of the next argument to be processed, which
               means that argv[optind-1] is the current one. We shall fail if
               more than one free argument is provided */
            if (*argv[optind - 1] == '-' || optind == argc
                    || *argv[optind] == '-'
                    || (optind + 1 < argc && *argv[optind + 1] != '-')) {
                demo_log(ERROR, "invalid pair OID SSID");
                goto finish;
            }

            AVS_LIST(access_entry_t) entry =
                    AVS_LIST_NEW_ELEMENT(access_entry_t);
            if (!entry) {
                goto finish;
            }

            // We ignore the fact, that someone passes bad input, and silently
            // truncate it to zero.
            long oid = strtol(argv[optind - 1], NULL, 10);
            long ssid = strtol(argv[optind], NULL, 10);
            entry->oid = (anjay_oid_t) oid;
            entry->ssid = (anjay_ssid_t) ssid;
            AVS_LIST_INSERT(&parsed_args->access_entries, entry);
            break;
        }
        case 'b':
            parsed_args->connection_args.servers[0].is_bootstrap = true;
            if (optarg && *optarg) {
                if (strcmp(optarg, "client-initiated-only") == 0) {
                    parsed_args->disable_legacy_server_initiated_bootstrap =
                            true;
                } else {
                    demo_log(ERROR,
                             "Invalid bootstrap optional argument: \"%s\"; "
                             "available options: client-initiated-only",
                             optarg);
                    goto finish;
                }
            }
            break;
        case 'H':
            if (parse_i32(optarg,
                          &parsed_args->connection_args.bootstrap_holdoff_s)) {
                goto finish;
            }
            break;
        case 'T':
            if (parse_i32(optarg,
                          &parsed_args->connection_args.bootstrap_timeout_s)) {
                goto finish;
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
            goto finish;
#ifndef _WIN32
        case 't':
            parsed_args->disable_stdin = true;
            break;
#endif // _WIN32
        case 'l':
            if (parse_i32(optarg, &parsed_args->connection_args.lifetime)) {
                goto finish;
            }
            break;
        case 'L':
            if (parse_size(optarg, &parsed_args->stored_notification_limit)) {
                goto finish;
            }
            break;
        case 'c':
            parsed_args->location_csv = optarg;
            break;
        case 'f': {
            long freq;
            if (demo_parse_long(optarg, &freq) || freq <= 0
                    || freq > INT32_MAX) {
                demo_log(ERROR, "invalid location update frequency: %s",
                         optarg);
                goto finish;
            }

            parsed_args->location_update_frequency_s = (time_t) freq;
            break;
        }
        case 'p': {
            long port;
            if (demo_parse_long(optarg, &port) || port <= 0
                    || port > UINT16_MAX) {
                demo_log(ERROR, "invalid UDP port number: %s", optarg);
                goto finish;
            }

            parsed_args->udp_listen_port = (uint16_t) port;
            break;
        }
        case 'i':
            if (parse_hexstring(optarg,
                                &parsed_args->connection_args
                                         .public_cert_or_psk_identity,
                                &parsed_args->connection_args
                                         .public_cert_or_psk_identity_size)) {
                demo_log(ERROR, "Invalid identity");
                goto finish;
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
                goto finish;
            }
            break;
        case 'K':
            key_path = optarg;
            break;
        case 'P':
            server_public_key_path = optarg;
            break;
        case 'q': {
            int idx = num_servers == 0 ? 0 : num_servers - 1;
            parsed_args->connection_args.servers[idx].binding_mode = optarg;
            break;
        }
        case 'D': {
            int idx = num_servers == 0 ? 0 : num_servers - 1;
            if (parse_u16(optarg,
                          &parsed_args->connection_args.servers[idx]
                                   .security_iid)) {
                goto finish;
            }
            break;
        }
        case 's':
            if (parse_security_mode(
                        optarg, &parsed_args->connection_args.security_mode)) {
                goto finish;
            }
            break;
        case 'd': {
            int idx = num_servers == 0 ? 0 : num_servers - 1;
            if (parse_u16(optarg,
                          &parsed_args->connection_args.servers[idx]
                                   .server_iid)) {
                goto finish;
            }
            break;
        }
        case 'u': {
            AVS_ASSERT(num_servers < MAX_SERVERS, "Too many servers");
            server_entry_t *entry =
                    &parsed_args->connection_args.servers[num_servers++];
            entry->uri = optarg;
            entry->security_iid = ANJAY_ID_INVALID;
            entry->server_iid = ANJAY_ID_INVALID;
            entry->binding_mode =
                    parsed_args->connection_args.servers[num_servers - 1]
                                    .binding_mode
                            ? parsed_args->connection_args
                                      .servers[num_servers - 1]
                                      .binding_mode
                            : DEFAULT_CMDLINE_ARGS.connection_args.servers[0]
                                      .binding_mode;
            break;
        }
        case 'I':
            if (parse_i32(optarg, &parsed_args->inbuf_size)
                    || parsed_args->inbuf_size <= 0) {
                goto finish;
            }
            break;
        case 'O':
            if (parse_i32(optarg, &parsed_args->outbuf_size)
                    || parsed_args->outbuf_size <= 0) {
                goto finish;
            }
            break;
        case '$':
            if (parse_i32(optarg, &parsed_args->msg_cache_size)
                    || parsed_args->msg_cache_size < 0) {
                goto finish;
            }
            break;
        case 'N':
            parsed_args->confirmable_notifications = true;
            break;
        case 'r': {
            int result;
            if (parse_i32(optarg, &result)
                    || result < (int) ANJAY_FW_UPDATE_RESULT_INITIAL
                    || result > (int) ANJAY_FW_UPDATE_RESULT_UNSUPPORTED_PROTOCOL) {
                demo_log(ERROR, "invalid update result value: %s", optarg);
                return -1;
            }
            parsed_args->fw_update_delayed_result =
                    (anjay_fw_update_result_t) result;
            break;
        }
        case 1:
            parsed_args->fw_updated_marker_path = optarg;
            break;
        case 2: {
            if (parsed_args->fw_security_info.mode
                    != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto finish;
            }

            const avs_net_certificate_info_t cert_info = {
                .server_cert_validation = true,
                .trusted_certs = avs_net_trusted_cert_info_from_file(optarg)
            };
            parsed_args->fw_security_info =
                    avs_net_security_info_from_certificates(cert_info);
            break;
        }
        case 3: {
            if (parsed_args->fw_security_info.mode
                    != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto finish;
            }
            const avs_net_certificate_info_t cert_info = {
                .server_cert_validation = true,
                .trusted_certs = avs_net_trusted_cert_info_from_path(optarg)
            };
            parsed_args->fw_security_info =
                    avs_net_security_info_from_certificates(cert_info);
            break;
        }
        case 4:
            if (parsed_args->fw_security_info.mode != AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.mode
                                   != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto finish;
            }
            if (parsed_args->fw_security_info.mode == AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.data.psk.identity) {
                demo_log(ERROR, "--fw-psk-identity specified more than once");
                goto finish;
            }
            if (parse_hexstring(optarg,
                                (uint8_t **) (intptr_t) &parsed_args
                                        ->fw_security_info.data.psk.identity,
                                &parsed_args->fw_security_info.data.psk
                                         .identity_size)) {
                demo_log(ERROR, "Invalid PSK identity for firmware upgrade");
                goto finish;
            }
            parsed_args->fw_security_info.mode = AVS_NET_SECURITY_PSK;
            break;
        case 5: {
            if (parsed_args->fw_security_info.mode != AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.mode
                                   != (avs_net_security_mode_t) -1) {
                demo_log(ERROR, "Multiple incompatible security information "
                                "specified for firmware upgrade");
                goto finish;
            }
            if (parsed_args->fw_security_info.mode == AVS_NET_SECURITY_PSK
                    && parsed_args->fw_security_info.data.psk.psk) {
                demo_log(ERROR, "--fw-psk-key specified more than once");
                goto finish;
            }

            uint8_t **psk_ptr = (uint8_t **) (intptr_t) &parsed_args
                                        ->fw_security_info.data.psk.psk;
            if (parse_hexstring(
                        optarg, psk_ptr,
                        &parsed_args->fw_security_info.data.psk.psk_size)) {
                demo_log(ERROR, "Invalid pre-shared key for firmware upgrade");
                goto finish;
            }
            parsed_args->fw_security_info.mode = AVS_NET_SECURITY_PSK;
            break;
        }
        case 6:
            parsed_args->attr_storage_file = optarg;
            break;
        case 12:
            if (parse_double(optarg,
                             &parsed_args->tx_params.ack_random_factor)) {
                demo_log(ERROR, "Expected ACK_RANDOM_FACTOR to be a floating "
                                "point number");
                goto finish;
            }
            break;
        case 13: {
            double ack_timeout_s;
            if (parse_double(optarg, &ack_timeout_s)) {
                demo_log(ERROR,
                         "Expected ACK_TIMEOUT to be a floating point number");
                goto finish;
            }
            parsed_args->tx_params.ack_timeout =
                    avs_time_duration_from_fscalar(ack_timeout_s, AVS_TIME_S);
            break;
        }
        case 14: {
            int32_t max_retransmit;
            if (parse_i32(optarg, &max_retransmit) || max_retransmit < 0) {
                demo_log(ERROR, "Expected MAX_RETRANSMIT to be an unsigned "
                                "integer");
                goto finish;
            }
            parsed_args->tx_params.max_retransmit = (unsigned) max_retransmit;
            break;
        }
        case 15: {
            double min_wait_s;
            if (parse_double(optarg, &min_wait_s) || min_wait_s <= 0) {
                demo_log(ERROR, "Expected DTLS_HS_RETRY_WAIT_MIN > 0");
                goto finish;
            }
            parsed_args->dtls_hs_tx_params.min =
                    avs_time_duration_from_fscalar(min_wait_s, AVS_TIME_S);
            break;
        }
        case 16: {
            double max_wait_s;
            if (parse_double(optarg, &max_wait_s) || max_wait_s <= 0) {
                demo_log(ERROR, "Expected DTLS_HS_RETRY_WAIT_MAX > 0");
                goto finish;
            }
            parsed_args->dtls_hs_tx_params.max =
                    avs_time_duration_from_fscalar(max_wait_s, AVS_TIME_S);
            break;
        }
        case 17:
            if (parse_double(optarg,
                             &parsed_args->fwu_tx_params.ack_random_factor)) {
                demo_log(ERROR, "Expected ACK_RANDOM_FACTOR to be a floating "
                                "point number");
                goto finish;
            }
            parsed_args->fwu_tx_params_modified = true;
            break;
        case 18: {
            double ack_timeout_s;
            if (parse_double(optarg, &ack_timeout_s)) {
                demo_log(ERROR,
                         "Expected ACK_TIMEOUT to be a floating point number");
                goto finish;
            }
            parsed_args->fwu_tx_params.ack_timeout =
                    avs_time_duration_from_fscalar(ack_timeout_s, AVS_TIME_S);
            parsed_args->fwu_tx_params_modified = true;
            break;
        }
        case 19: {
            int32_t max_retransmit;
            if (parse_i32(optarg, &max_retransmit) || max_retransmit < 0) {
                demo_log(ERROR, "Expected MAX_RETRANSMIT to be an unsigned "
                                "integer");
                goto finish;
            }
            parsed_args->fwu_tx_params.max_retransmit =
                    (unsigned) max_retransmit;
            parsed_args->fwu_tx_params_modified = true;
            break;
        }
        case 20:
            parsed_args->prefer_hierarchical_formats = true;
            break;
        case 22:
            parsed_args->use_connection_id = true;
            break;
        case 23: {
            char *saveptr = NULL;
            char *str = optarg;
            const char *token;
            while ((token = avs_strtok(str, ",", &saveptr))) {
                uint32_t *reallocated = (uint32_t *) avs_realloc(
                        parsed_args->default_ciphersuites,
                        sizeof(*parsed_args->default_ciphersuites)
                                * ++parsed_args->default_ciphersuites_count);
                if (!reallocated) {
                    demo_log(ERROR, "Out of memory");
                    goto finish;
                }
                parsed_args->default_ciphersuites = reallocated;
                if (parse_u32(token,
                              &parsed_args->default_ciphersuites
                                       [parsed_args->default_ciphersuites_count
                                        - 1])) {
                    demo_log(ERROR, "Invalid ciphersuite ID: %s", token);
                    goto finish;
                }
                str = NULL;
            }
            break;
        }
        case 0:
            goto process;
        }
    }
process:
    for (int i = 0; i < AVS_MAX(num_servers, 1); ++i) {
        server_entry_t *entry = &parsed_args->connection_args.servers[i];
        entry->id = (anjay_ssid_t) (i + 1);
        if (entry->security_iid == ANJAY_ID_INVALID) {
            entry->security_iid = (anjay_iid_t) entry->id;
        }
        if (entry->server_iid == ANJAY_ID_INVALID) {
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
        goto finish;
    }
    if (parsed_args->connection_args.security_mode == ANJAY_SECURITY_PSK) {
        if (!identity_set
                && clone_buffer(&parsed_args->connection_args
                                         .public_cert_or_psk_identity,
                                &parsed_args->connection_args
                                         .public_cert_or_psk_identity_size,
                                DEFAULT_PSK_IDENTITY,
                                sizeof(DEFAULT_PSK_IDENTITY) - 1)) {
            goto finish;
        }
        if (!key_set
                && clone_buffer(&parsed_args->connection_args
                                         .private_cert_or_psk_key,
                                &parsed_args->connection_args
                                         .private_cert_or_psk_key_size,
                                DEFAULT_PSK_KEY, sizeof(DEFAULT_PSK_KEY) - 1)) {
            goto finish;
        }
    } else if (parsed_args->connection_args.security_mode
               == ANJAY_SECURITY_CERTIFICATE) {
        if (identity_set ^ key_set) {
            demo_log(ERROR, "Setting public cert but not private cert (and "
                            "other way around) makes little sense");
            goto finish;
        } else if (!identity_set) {
            if (load_buffer_from_file(
                        &parsed_args->connection_args
                                 .public_cert_or_psk_identity,
                        &parsed_args->connection_args
                                 .public_cert_or_psk_identity_size,
                        cert_path)) {
                demo_log(ERROR, "Could not load certificate from %s",
                         cert_path);
                goto finish;
            }
            if (load_buffer_from_file(
                        &parsed_args->connection_args.private_cert_or_psk_key,
                        &parsed_args->connection_args
                                 .private_cert_or_psk_key_size,
                        key_path)) {
                demo_log(ERROR, "Could not load private key from %s", key_path);
                goto finish;
            }
        }
        if (server_public_key_path
                && load_buffer_from_file(
                           &parsed_args->connection_args.server_public_key,
                           &parsed_args->connection_args.server_public_key_size,
                           server_public_key_path)) {
            demo_log(ERROR, "Could not load server public key from %s",
                     server_public_key_path);
            goto finish;
        }
    }
    if (parsed_args->fw_security_info.mode == AVS_NET_SECURITY_PSK
            && (!parsed_args->fw_security_info.data.psk.identity
                || !parsed_args->fw_security_info.data.psk.psk)) {
        demo_log(ERROR, "Both identity and key must be provided when using PSK "
                        "for firmware upgrade security");
        goto finish;
    }
    retval = 0;
finish:
    if (retval) {
        AVS_LIST_CLEAR(&parsed_args->access_entries);
        avs_free(parsed_args->default_ciphersuites);
        parsed_args->default_ciphersuites = NULL;
    }
    avs_free(default_cert_path);
    avs_free(default_key_path);
    return retval;
}
