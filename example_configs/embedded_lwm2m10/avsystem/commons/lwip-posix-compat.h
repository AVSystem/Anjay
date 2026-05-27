/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Commons library
 * All rights reserved.
 *
 * Licensed under AVSystem Commons library - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#ifndef COMPAT_H
#define COMPAT_H

/*
 * Example implementation of a AVS_COMMONS_POSIX_COMPAT_HEADER file required for
 * non-POSIX platforms that use LwIP 1.4.1.
 *
 * Contains all types/macros/symbols not defined in core C that are required
 * to compile avs_commons library.
 */

#include "lwipopts.h"

/* Provides lwIP's alternative errno header, used in avs_net_impl.c */
#include "lwip/errno.h"

/* Provides htons/ntohs/htonl/ntohl */
#include "lwip/inet.h"

/* Provides e.g. LWIP_VERSION_* macros used in net_impl.c */
#include "lwip/init.h"

/*
 * Provides:
 * - POSIX-compatible socket API, socklen_t,
 * - fcntl, F_GETFL, F_SETFL, O_NONBLOCK,
 * - select, struct fd_set, FD_SET, FD_CLEAR, FD_ISSET
 */
#include "lwip/sockets.h"

/* Provides getaddrinfo/freeaddrinfo/struct addrinfo */
#include "lwip/netdb.h"

#if LWIP_VERSION_MAJOR >= 2
#    define AVS_COMMONS_NET_POSIX_AVS_SOCKET_HAVE_INET_NTOP
#endif // LWIP_VERSION_MAJOR >= 2

typedef int sockfd_t;

#endif /* COMPAT_H */
