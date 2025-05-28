/*
 * Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
 * See the attached LICENSE file for details.
 */

#ifndef LWM2M_GATEWAY_H
#define LWM2M_GATEWAY_H

#include <anjay/anjay_config.h>

#ifdef ANJAY_WITH_LWM2M_GATEWAY

#    include <anjay/anjay.h>

#    define LWM2M_GATEWAY_END_DEVICE_COUNT 2
// keep it equal to LWM2M_GATEWAY_END_DEVICE_COUNT - 1
#    define LWM2M_GATEWAY_END_DEVICE_RANGE 1

int lwm2m_gateway_setup(anjay_t *anjay);
void lwm2m_gateway_cleanup(anjay_t *anjay);

int lwm2m_gateway_setup_end_device(anjay_t *anjay, anjay_iid_t iid);
void lwm2m_gateway_cleanup_end_device(anjay_t *anjay, anjay_iid_t iid);

void lwm2m_gateway_press_button_end_device(anjay_t *anjay, anjay_iid_t iid);
void lwm2m_gateway_release_button_end_device(anjay_t *anjay, anjay_iid_t iid);

void lwm2m_gateway_binary_app_data_container_write(anjay_t *anjay,
                                                   uint16_t dev_no,
                                                   anjay_iid_t iid,
                                                   anjay_riid_t riid,
                                                   const char *value);

#endif // ANJAY_WITH_LWM2M_GATEWAY

#endif // LWM2M_GATEWAY_H
