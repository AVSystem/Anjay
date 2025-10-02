/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#include <avsystem/commons/avs_commons_config.h>

#ifdef WITH_DEMO_TRAFFIC_INTERCEPTOR

#    include <errno.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <sys/un.h>
#    include <unistd.h>

#    include <avsystem/coap/udp.h>
#    include <avsystem/commons/avs_net.h>
#    include <avsystem/commons/avs_socket_v_table.h>
#    include <avsystem/commons/avs_stream_membuf.h>
#    include <avsystem/commons/avs_time.h>
#    include <avsystem/commons/avs_utils.h>

#    include "demo_utils.h"
#    include "net_traffic_interceptor.h"

static int g_interceptor_sock;
static const char *g_endpoint_name;

static char *
direction_to_string(avs_net_traffic_interceptor_direction_t direction) {
    switch (direction) {
    case AVS_NET_TRAFFIC_INTERCEPTOR_INCOMING:
        return "incoming";
    case AVS_NET_TRAFFIC_INTERCEPTOR_OUTGOING:
        return "outgoing";
    default:
        AVS_UNREACHABLE();
    }
}

static char *transport_to_string(avs_net_socket_type_t type) {
    switch (type) {
    case AVS_NET_TCP_SOCKET:
        return "TCP";
    case AVS_NET_UDP_SOCKET:
        return "UDP";
    case AVS_NET_SSL_SOCKET:
        return "SSL";
    case AVS_NET_DTLS_SOCKET:
        return "DTLS";
    default:
        AVS_UNREACHABLE();
    }
}

void _avs_net_traffic_interceptor(
        avs_net_socket_t *socket,
        const void *data,
        size_t data_length,
        avs_net_socket_type_t type,
        avs_net_traffic_interceptor_direction_t direction) {
    if (g_interceptor_sock < 0) {
        return;
    }
    char *buffer = NULL;
    avs_stream_t *membuf = avs_stream_membuf_create();
    if (!membuf) {
        demo_log(ERROR, "out of memory");
        return;
    }

    char *remote_host = (char *) malloc(INET6_ADDRSTRLEN);
    if (!remote_host) {
        demo_log(ERROR, "Out of memory");
        goto clean_up;
    }
    avs_net_socket_get_remote_host(socket, remote_host, INET6_ADDRSTRLEN);
    char remote_port[sizeof(uint16_t) * 2 + 1];
    avs_net_socket_get_remote_port(socket, remote_port, sizeof(remote_port));
    long timestamp;
    avs_time_real_to_scalar(&timestamp, AVS_TIME_S, avs_time_real_now());
    if (avs_is_err(avs_stream_write_f(membuf,
                                      "{\n"
                                      "  \"endpoint_name\": \"%s\",\n"
                                      "  \"remote_host\": \"%s\",\n"
                                      "  \"remote_port\": \"%s\",\n"
                                      "  \"direction\": \"%s\",\n"
                                      "  \"timestamp\": %ld,\n"
                                      "  \"transport\": \"%s\",\n"
                                      "  \"transport_payload\": \"",
                                      g_endpoint_name, remote_host, remote_port,
                                      direction_to_string(direction), timestamp,
                                      transport_to_string(type)))) {
        demo_log(ERROR, "avs_stream_write_f failed");
        goto clean_up;
    }

    size_t hexlified_len;
    buffer = (char *) malloc(data_length * 2 + 1);
    if (avs_hexlify(buffer, data_length * 2 + 1, &hexlified_len, data,
                    data_length)
            || hexlified_len != data_length) {
        demo_log(ERROR, "avs_hexlify failed");
        goto clean_up;
    }
    hexlified_len *= 2;
    if (avs_is_err(avs_stream_write_f(membuf, "%s\"\n}", buffer))) {
        demo_log(ERROR, "avs_stream_write_f failed");
        goto clean_up;
    }
    void *buf_ptr;
    size_t buf_len;
    if (avs_is_err(
                avs_stream_membuf_take_ownership(membuf, &buf_ptr, &buf_len))) {
        demo_log(ERROR, "avs_stream_membuf_take_ownership failed");
        goto clean_up;
    }
    send(g_interceptor_sock, buf_ptr, buf_len, 0);
    free(buf_ptr);
clean_up:
    if (avs_is_err(avs_stream_cleanup(&membuf))) {
        demo_log(ERROR, "avs_stream_cleanup failed");
    }
    free(buffer);
    free(remote_host);
}

int interceptor_init(const char *socket_path, const char *endpoint_name) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    errno = 0;
    int res =
            connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un));
    if (res < 0) {
        demo_log(ERROR, "Traffic interceptor failed connecting to: %s",
                 socket_path);
        close(fd);
        return -1;
    }
    g_interceptor_sock = fd;
    g_endpoint_name = endpoint_name;
    return 0;
}

int interceptor_deinit(void) {
    if (g_interceptor_sock >= 0) {
        const int fd = g_interceptor_sock;
        g_interceptor_sock = -1;
        return close(fd);
    }
    g_interceptor_sock = -1;
    return 0;
}

#endif // WITH_DEMO_TRAFFIC_INTERCEPTOR
