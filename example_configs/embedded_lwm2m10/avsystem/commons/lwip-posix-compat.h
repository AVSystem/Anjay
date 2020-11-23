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
