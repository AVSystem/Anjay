/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJ_CONFIG_H
#define ANJ_CONFIG_H

#define DM_WITH_LOGS
#define ANJ_WITH_SDM_LOGS
#define WITH_DDM
#define ANJ_WITH_DEFAULT_SECURITY_OBJ
#define ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER 1
#define ANJ_PUBLIC_KEY_OR_IDENTITY_MAX_SIZE 255
#define ANJ_SERVER_PUBLIC_KEY_MAX_SIZE 255
#define ANJ_SECRET_KEY_MAX_SIZE 255
#define ANJ_WITH_DEFAULT_SERVER_OBJ
#define ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER 1
#define ANJ_WITH_DEFAULT_DEVICE_OBJ
#define ANJ_WITH_FOTA_OBJECT
#define ANJ_FOTA_PULL_METHOD_SUPPORTED
#define ANJ_FOTA_PUSH_METHOD_SUPPORTED
#define ANJ_FOTA_PROTOCOL_COAP_SUPPORTED
#define ANJ_FOTA_PROTOCOL_COAPS_SUPPORTED
#define ANJ_FOTA_PROTOCOL_HTTP_SUPPORTED
#define ANJ_FOTA_PROTOCOL_HTTPS_SUPPORTED
#define ANJ_FOTA_PROTOCOL_COAP_TCP_SUPPORTED
#define ANJ_FOTA_PROTOCOL_COAP_TLS_SUPPORTED

#if defined(ANJ_WITH_DEFAULT_SECURITY_OBJ)                      \
        && (!defined(ANJ_SECURITY_OBJ_ALLOWED_INSTANCES_NUMBER) \
            || !defined(ANJ_PUBLIC_KEY_OR_IDENTITY_MAX_SIZE)    \
            || !defined(ANJ_SERVER_PUBLIC_KEY_MAX_SIZE)         \
            || !defined(ANJ_SECRET_KEY_MAX_SIZE))
#    error "if default Security Object is enabled, its parameters needs to be defined"
#endif

#if defined(ANJ_WITH_DEFAULT_SERVER_OBJ) \
        && !defined(ANJ_SERVER_OBJ_ALLOWED_INSTANCES_NUMBER)
#    error "if default Server Object is enabled, its allowed instances number needs to be defined"
#endif

#if defined(ANJ_WITH_FOTA_OBJECT) && !defined(ANJ_FOTA_PULL_METHOD_SUPPORTED) \
        && !defined(ANJ_FOTA_PUSH_METHOD_SUPPORTED)
#    error "if FW Update object is enabled, at least one of push or pull methods needs to be enabled"
#endif

#endif // ANJ_CONFIG_H
