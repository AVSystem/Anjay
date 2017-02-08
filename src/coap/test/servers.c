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

#ifdef ANJAY_TEST

#include "servers.h"

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>

#if __linux__
#include <sys/prctl.h>
#endif // __linux__

#include <avsystem/commons/list.h>
#include <avsystem/commons/unit/test.h>

#include <anjay_test/mock_clock.h>

#include "../../utils.h"
#include "../msg.h"
#include "../msg_internal.h"
#include "../log.h"

typedef struct response_state {
    bool has_more_responses;
    size_t msg_counter;
    size_t response_counter;
} response_state_t;

typedef ssize_t make_response_func_t(response_state_t *state,
                                     const char *in,
                                     size_t in_size,
                                     char *out,
                                     size_t out_size);

typedef enum socket_type {
    TYPE_DTLS,
    TYPE_UDP
} socket_type_t;

typedef struct server {
    int pid;
    uint16_t port;
    socket_type_t type;
    make_response_func_t *make_response;
} server_t;

static AVS_LIST(server_t) dtls_servers = NULL;
static AVS_LIST(server_t) udp_servers = NULL;

static void kill_servers(void) {
    AVS_LIST_CLEAR(&dtls_servers) {
        kill(dtls_servers->pid, SIGTERM);
    }
    AVS_LIST_CLEAR(&udp_servers) {
        kill(udp_servers->pid, SIGTERM);
    }
}

static void notify_parent(void) {
    kill(getppid(), SIGUSR1);
}

static void set_sigusr1_mask(int action) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigprocmask(action, &set, NULL);
}

static void wait_for_child(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigwaitinfo(&set, NULL);
}

static void spawn_dtls_echo_server(uint16_t port) {
    server_t *serv;
    AVS_LIST_FOREACH(serv, dtls_servers) {
        if (serv->port == port) {
            if (serv->type != TYPE_DTLS) {
                coap_log(ERROR, "another server running on port %u", port);
                abort();
            }
            return;
        }
    }

    char cmdline[] = ANJAY_BIN_DIR "/dtls_echo_server\0"
                     "-p\0"
                     "_____";
    char *args[4] = { cmdline };

    for (size_t i = 1; i < sizeof(args) / sizeof(args[0]) - 1; ++i) {
        args[i] = args[i - 1] + strlen(args[i - 1]) + 1;
    }

    AVS_UNIT_ASSERT_TRUE(_anjay_snprintf(args[2], strlen(args[2]), "%u", port) >= 0);

    set_sigusr1_mask(SIG_BLOCK);

    int pid = -1;
    switch (pid = fork()) {
    case 0:
#if __linux__
        if (prctl(PR_SET_PDEATHSIG, SIGHUP)) {
            coap_log(WARNING, "prctl failed: %s", strerror(errno));
        }
#endif // __linux__
        execve(args[0], args, NULL);
    case -1:
        coap_log(ERROR, "could not start DTLS echo server: %s", strerror(errno));
        coap_log(ERROR, "command: %s %s %s", args[0], args[1], args[2]);
        abort();
    default:
        break;
    }

    atexit(kill_servers);

    serv = AVS_LIST_NEW_ELEMENT(server_t);
    AVS_UNIT_ASSERT_NOT_NULL(serv);
    serv->pid = pid;
    serv->port = port;
    serv->type = TYPE_DTLS;
    AVS_LIST_INSERT(&dtls_servers, serv);

    wait_for_child();
    set_sigusr1_mask(SIG_UNBLOCK);
}

static void udp_serve(uint16_t port,
                      make_response_func_t *make_response) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (!inet_aton("127.0.0.1", &addr.sin_addr)
            || bind(sock, (struct sockaddr*)&addr, sizeof(addr))) {
        coap_log(ERROR, "UDP server (127.0.0.1:%u) bind failed: %s",
                 port, strerror(errno));
        goto cleanup;
    }

    notify_parent();

    char in_buffer[65535];
    char out_buffer[65535];
    struct pollfd sock_pollfd = {
        .fd = sock,
        .events = POLLIN,
        .revents = 0
    };

    response_state_t state;
    memset(&state, 0, sizeof(state));

    while (true) {
        if (poll(&sock_pollfd, 1, -1) < 0) {
            coap_log(ERROR, "UDP server (127.0.0.1:%u) poll failed: %s",
                     port, strerror(errno));
            goto cleanup;
        }

        struct sockaddr_in remote_addr;
        memset(&remote_addr, 0, sizeof(remote_addr));
        socklen_t remote_addr_len = sizeof(remote_addr);

        ssize_t bytes_recv = recvfrom(sock, in_buffer, sizeof(in_buffer), 0,
                                      (struct sockaddr*)&remote_addr,
                                      &remote_addr_len);
        if (bytes_recv < 0) {
            coap_log(ERROR, "UDP server (127.0.0.1:%u) recvfrom failed: %s",
                     port, strerror(errno));
            goto cleanup;
        }
        ++state.msg_counter;

        do {
            state.has_more_responses = false;
            ssize_t bytes_to_send = make_response(&state,
                                                  in_buffer, (size_t)bytes_recv,
                                                  out_buffer, sizeof(out_buffer));
            if (bytes_to_send < 0) {
                coap_log(ERROR, "UDP server (127.0.0.1:%u) make_response failed",
                         port);
                goto cleanup;
            }

            if (sendto(sock, out_buffer, (size_t)bytes_to_send, 0,
                       (struct sockaddr*)&remote_addr, remote_addr_len) != bytes_to_send) {
                coap_log(ERROR, "UDP server (127.0.0.1:%u) sendto failed: %s",
                         port, strerror(errno));
                goto cleanup;
            }

            ++state.response_counter;
        } while (state.has_more_responses);
    }

cleanup:
    close(sock);
    coap_log(ERROR, "UDP server (127.0.0.1:%u) shutting down", port);
}

static void spawn_udp_server(uint16_t port,
                             make_response_func_t *make_response) {
    server_t *serv;
    AVS_LIST_FOREACH(serv, udp_servers) {
        if (serv->port == port) {
            if (serv->type != TYPE_UDP
                    || serv->make_response != make_response) {
                coap_log(ERROR, "another server running on port %u", port);
                abort();
            }
            return;
        }
    }

    set_sigusr1_mask(SIG_BLOCK);

    int pid = -1;
    switch (pid = fork()) {
    case 0:
#if __linux__
        if (prctl(PR_SET_PDEATHSIG, SIGHUP)) {
            coap_log(WARNING, "prctl failed: %s", strerror(errno));
        }
#endif // __linux__
        udp_serve(port, make_response);
    case -1:
        coap_log(ERROR, "could not start UDP server on port %u: %s",
                 port, strerror(errno));
        abort();
    default:
        break;
    }

    atexit(kill_servers);

    serv = AVS_LIST_NEW_ELEMENT(server_t);
    AVS_UNIT_ASSERT_NOT_NULL(serv);
    serv->pid = pid;
    serv->port = port;
    serv->type = TYPE_UDP;
    serv->make_response = make_response;
    AVS_LIST_INSERT(&udp_servers, serv);

    wait_for_child();
    set_sigusr1_mask(SIG_UNBLOCK);
}

static anjay_coap_socket_t *setup_socket(socket_type_t type,
                                         uint16_t port,
                                         make_response_func_t *make_response) {
    switch (type) {
    case TYPE_DTLS: spawn_dtls_echo_server(port); break;
    case TYPE_UDP:  spawn_udp_server(port, make_response); break;
    }

    bool use_nosec = (type == TYPE_UDP);
    anjay_coap_socket_t *socket = NULL;
    avs_net_abstract_socket_t *backend = NULL;
    avs_net_ssl_configuration_t config = {
        .version = AVS_NET_SSL_VERSION_DEFAULT,
        .security = {
            .mode = AVS_NET_SECURITY_CERTIFICATE,
            .data.cert = {
                .server_cert_validation = true,
                .trusted_certs = avs_net_trusted_cert_source_from_paths(
                                        NULL, "certs/root.crt"),
                .client_cert = avs_net_client_cert_from_file(
                                        "certs/client.crt", NULL,
                                        AVS_NET_DATA_FORMAT_PEM),
                .client_key = avs_net_private_key_from_file(
                                        "certs/client.key", NULL,
                                        AVS_NET_DATA_FORMAT_PEM)
            }
        },
        .backend_configuration = {
            .address_family = AVS_NET_AF_INET4
        }
    };

    char port_str[8];
    AVS_UNIT_ASSERT_TRUE(_anjay_snprintf(port_str, sizeof(port_str), "%u", port) >= 0);

    avs_net_socket_type_t sock_type = use_nosec ? AVS_NET_UDP_SOCKET
                                                : AVS_NET_DTLS_SOCKET;
    void *sock_config = use_nosec ? (void *) &config.backend_configuration
                                  : (void *) &config;
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_create(&backend, sock_type, sock_config));
    // this doesn't actually do anything,
    // but ensures that bind() and connect() can be used together
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_bind(backend, NULL, NULL));
    AVS_UNIT_ASSERT_SUCCESS(avs_net_socket_connect(backend, "localhost", port_str));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_socket_create(&socket, backend));

    return socket;
}

static ssize_t echo(response_state_t *state,
                    const char *in,
                    size_t in_size,
                    char *out,
                    size_t out_size) {
    (void)state;

    if (in_size > out_size) {
        return -1;
    }

    memcpy(out, in, in_size);
    return (ssize_t)in_size;
}

#define VTTL(version, type, token_length) \
    (uint8_t)((((version) & 0x03) << 6) | (((type) & 0x03) << 4) | ((token_length) & 0x0f))

static ssize_t fill_header_with_token_and_id(anjay_coap_msg_type_t type,
                                             const char *in,
                                             size_t in_size,
                                             char *out,
                                             size_t out_size) {
    if (in_size < ANJAY_COAP_MSG_MIN_SIZE) {
        return -1;
    }

    uint8_t token_length = *(const uint8_t*)in & 0x0F;
    if (token_length > ANJAY_COAP_MAX_TOKEN_LENGTH
            || in_size < ANJAY_COAP_MSG_MIN_SIZE + token_length
            || out_size < sizeof(anjay_coap_msg_header_t) + token_length) {
        return -1;
    }

    out[0] = (char)VTTL(1, type, token_length);
    out[1] = (char)ANJAY_COAP_CODE_CONTENT,
    out[2] = (char)in[2];
    out[3] = (char)in[3];

    memcpy(out + sizeof(anjay_coap_msg_header_t),
           in + sizeof(anjay_coap_msg_header_t), token_length);

    return (ssize_t)(sizeof(anjay_coap_msg_header_t) + token_length);
}

static ssize_t ack_echo(response_state_t *state,
                        const char *in,
                        size_t in_size,
                        char *out,
                        size_t out_size) {
    (void)state;

    if (out_size < in_size) {
        return -1;
    }

    memcpy(out, in, in_size);
    if (fill_header_with_token_and_id(ANJAY_COAP_MSG_ACKNOWLEDGEMENT,
                                      in, in_size, out, out_size) < 0) {
        return -1;
    }
    return (ssize_t)in_size;
}

static ssize_t reset(response_state_t *state,
                     const char *in,
                     size_t in_size,
                     char *out,
                     size_t out_size) {
    (void)state;
    return fill_header_with_token_and_id(ANJAY_COAP_MSG_RESET, in, in_size,
                                         out, out_size);
}

static ssize_t mismatched_ack_then_reset(response_state_t *state,
                                         const char *in,
                                         size_t in_size,
                                         char *out,
                                         size_t out_size) {
    bool mismatched = state->response_counter % 2 == 0;

    ssize_t retval;
    if (mismatched) {
        retval = ack_echo(NULL, in, in_size, out, out_size);
        if (retval >= 4) {
            out[2] = (char) ~out[2];
            out[3] = (char) ~out[3];
        }
    } else {
        retval = fill_header_with_token_and_id(ANJAY_COAP_MSG_RESET,
                                               in, in_size, out, out_size);
    }

    return retval;
}

static ssize_t mismatched_reset_then_ack(response_state_t *state,
                                         const char *in,
                                         size_t in_size,
                                         char *out,
                                         size_t out_size) {
    bool mismatched = state->response_counter % 2 == 0;

    ssize_t retval;
    if (mismatched) {
        retval = fill_header_with_token_and_id(ANJAY_COAP_MSG_RESET,
                                               in, in_size, out, out_size);
        if (retval >= 4) {
            out[2] = (char) ~out[2];
            out[3] = (char) ~out[3];
        }
    } else {
        retval = ack_echo(NULL, in, in_size, out, out_size);
    }

    return retval;
}

static ssize_t fill_garbage(char *out,
                            size_t out_size) {
    ssize_t msg_size = ANJAY_MIN(1024, (ssize_t)out_size);
    for (ssize_t i = 0; i < msg_size; ++i) {
        out[i] = (char)rand();
    }
    return msg_size;
}

static ssize_t garbage_then_ack(response_state_t *state,
                                const char *in,
                                size_t in_size,
                                char *out,
                                size_t out_size) {
    bool send_garbage = state->response_counter % 2 == 0;

    if (send_garbage) {
        state->has_more_responses = true;
        return fill_garbage(out, out_size);
    }

    return ack_echo(NULL, in, in_size, out, out_size);
}

static ssize_t garbage(response_state_t *state,
                       const char *in,
                       size_t in_size,
                       char *out,
                       size_t out_size) {
    (void)state;
    (void)in;
    (void)in_size;
    return fill_garbage(out, out_size);
}

static ssize_t ack(response_state_t *state,
                   const char *in,
                   size_t in_size,
                   char *out,
                   size_t out_size) {
    (void) state;
    if (in_size < 4 || out_size < 4) {
        return -1;
    }

    out[0] = (char) VTTL(1, ANJAY_COAP_MSG_ACKNOWLEDGEMENT, 0);
    out[1] = (char) ANJAY_COAP_CODE_EMPTY;
    out[2] = in[2];
    out[3] = in[3];
    return 4;
}

static ssize_t long_separate(response_state_t *state,
                             const char *in,
                             size_t in_size,
                             char *out,
                             size_t out_size) {
    static char SAVED_DATA[2048] = { 0 };
    static size_t SAVED_DATA_LENGTH;
    assert(in_size < sizeof(SAVED_DATA));
    assert(in_size >= 4);
    assert(out_size >= 4);

    uint8_t token_length = _anjay_coap_msg_header_get_token_length(
            (const anjay_coap_msg_header_t *) SAVED_DATA);

    switch (state->response_counter % 3) {
    case 0:
        // empty ACK
        SAVED_DATA_LENGTH = in_size;
        memcpy(SAVED_DATA, in, in_size);
        state->has_more_responses = true;
        return ack(NULL, in, in_size, out, out_size);
    case 1:
        // mismatched reply
        assert((size_t) (token_length + 4) <= SAVED_DATA_LENGTH);
        assert(out_size >= SAVED_DATA_LENGTH);
        state->has_more_responses = true;
        memcpy(&out[4], &SAVED_DATA[4], token_length);
        for (size_t i = 0; i < token_length; ++i) {
            out[i] = (char) ~out[i];
        }
        break;
    case 2:
        // proper reply
        assert((size_t) (token_length + 4) <= SAVED_DATA_LENGTH);
        assert(out_size >= SAVED_DATA_LENGTH);
        memcpy(&out[4], &SAVED_DATA[4], token_length);
        break;
    }

    out[0] = (char) VTTL(1, ANJAY_COAP_MSG_CONFIRMABLE, token_length);
    out[1] = (char) ANJAY_COAP_CODE_CONTENT;
    do {
        out[2] = (char) rand();
        out[3] = (char) rand();
    } while (out[2] != SAVED_DATA[2] || out[3] != SAVED_DATA[3]);
    memcpy(&out[4 + token_length], &SAVED_DATA[4 + token_length],
           (size_t) (SAVED_DATA_LENGTH - (size_t) (4 + token_length)));
    return (ssize_t) SAVED_DATA_LENGTH;
}

anjay_coap_socket_t *_anjay_test_setup_dtls_echo_socket(uint16_t port) {
    return setup_socket(TYPE_DTLS, port, NULL);
}

anjay_coap_socket_t *_anjay_test_setup_udp_echo_socket(uint16_t port) {
    return setup_socket(TYPE_UDP, port, echo);
}

anjay_coap_socket_t *_anjay_test_setup_udp_ack_echo_socket(uint16_t port) {
    return setup_socket(TYPE_UDP, port, ack_echo);
}

anjay_coap_socket_t *_anjay_test_setup_udp_reset_socket(uint16_t port) {
    return setup_socket(TYPE_UDP, port, reset);
}

anjay_coap_socket_t *_anjay_test_setup_udp_mismatched_ack_then_reset_socket(uint16_t port) {
    return setup_socket(TYPE_UDP, port, mismatched_ack_then_reset);
}

anjay_coap_socket_t *_anjay_test_setup_udp_garbage_socket(uint16_t port) {
    return setup_socket(TYPE_UDP, port, garbage);
}

anjay_coap_socket_t *_anjay_test_setup_udp_mismatched_reset_then_ack_socket(uint16_t port) {
    return setup_socket(TYPE_UDP, port, mismatched_reset_then_ack);
}

anjay_coap_socket_t *_anjay_test_setup_udp_garbage_then_ack_socket(uint16_t port) {
    return setup_socket(TYPE_UDP, port, garbage_then_ack);
}

anjay_coap_socket_t *_anjay_test_setup_udp_long_separate_socket(uint16_t port) {
    return setup_socket(TYPE_UDP, port, long_separate);
}

#else // ANJAY_TEST

typedef int iso_c_forbids_an_empty_translation_unit;

#endif // ANJAY_TEST
