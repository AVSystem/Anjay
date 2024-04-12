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

#include <avsystem/commons/avs_log.h>

#include <wifi_init.hpp>

void wifi_init() {
#if defined(ARDUINO_ESP32_DEV)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
#elif defined(ARDUINO_SAMD_NANO_33_IOT)
    if (WiFi.status() == WL_NO_MODULE) {
        avs_log(wifi, ERROR, "Communication with Wi-Fi module failed");
        while (true)
            ;
    }
#endif
    do {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        avs_log(wifi, WARNING, "Communication with Wi-Fi module failed");
        delay(5000);
    } while (WiFi.status() != WL_CONNECTED);

    avs_log(wifi, INFO, "Connected to %s", WIFI_SSID);
}
