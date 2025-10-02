/*
 * Copyright 2017-2026 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#ifndef NET_TRAFFIC_INTERCEPTOR_H
#define NET_TRAFFIC_INTERCEPTOR_H

#ifdef WITH_DEMO_TRAFFIC_INTERCEPTOR
int interceptor_init(const char *socket_path, const char *endpoint_name);
int interceptor_deinit(void);
#endif // WITH_DEMO_TRAFFIC_INTERCEPTOR

#endif // NET_TRAFFIC_INTERCEPTOR_H
