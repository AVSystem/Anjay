/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <Arduino.h>

#include <WiFi.h>
#include <WiFiUdp.h>

#include <avsystem/commons/avs_log.h>

#include <anj/anj_time.h>

uint64_t anj_time_now() {
    static uint64_t prev_tick = 0;
    static uint64_t base_tick = 0;

    uint64_t ticks = millis();
    if (ticks < prev_tick) {
        base_tick += (uint64_t) UINT32_MAX + 1;
    }
    prev_tick = ticks;
    ticks += base_tick;

    return ticks;
}

uint64_t anj_time_real_now() {
    return anj_time_now();
}
