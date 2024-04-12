/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <Arduino.h>

#include <anj/sdm_device_object.h>
#include <anj/sdm_impl.h>
#include <anj/sdm_io.h>
#include <anj/sdm_security_object.h>
#include <anj/sdm_server_object.h>

#include <avsystem/commons/avs_log.h>

#include <event_loop.hpp>
#include <temperature_object.hpp>
#include <wifi_init.hpp>

#if defined(ARDUINO_ESP32_DEV)
#    include <Esp.h>
#endif // defined(ARDUINO_ESP32_DEV)

static bool should_reboot;

static int reboot_cb(sdm_obj_t *obj,
                     sdm_obj_inst_t *obj_inst,
                     sdm_res_t *res,
                     const char *execute_arg,
                     size_t execute_arg_len) {
    should_reboot = true;
    return 0;
}

static void device_object_reboot_if_needed(void) {
    if (!should_reboot) {
        return;
    }
#if defined(ARDUINO_ESP32_DEV)
    ESP.restart();
#elif defined(ARDUINO_SAMD_NANO_33_IOT)
    NVIC_SystemReset();
#endif
}

static sdm_device_object_init_t device_obj_conf = {
    .manufacturer = "AVSystem",
    .model_number = "PlatformIO",
    .serial_number = "2024",
    .firmware_version = "2024",
    .reboot_handler = reboot_cb,
    .supported_binding_modes = "U"
};

static sdm_server_instance_init_t server_inst = {
    .ssid = 1,
    .lifetime = 20,
    .default_min_period = 0,
    .default_max_period = 0,
    .notification_storing = false,
    .binding = "U",
    .bootstrap_on_registration_failure = NULL,
    .mute_send = false,
    .iid = NULL
};

static sdm_security_instance_init_t security_inst = {
    .server_uri = LWM2M_SERVER_URI,
    .bootstrap_server = false,
    .security_mode = SDM_SECURITY_NOSEC,
    .public_key_or_identity = NULL,
    .public_key_or_identity_size = 0,
    .server_public_key = NULL,
    .server_public_key_size = 0,
    .secret_key = NULL,
    .secret_key_size = 0,
    .ssid = 1,
    .iid = NULL
};

static event_loop_ctx_t event_loop;

static void
log_handler(avs_log_level_t level, const char *module, const char *message) {
    (void) level;
    (void) module;
    Serial.println(message);
}

void setup() {
    Serial.begin(115200);
    avs_log_set_handler(log_handler);

#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
#endif // LED_BUILTIN

    wifi_init();

    if (event_loop_init(&event_loop, LWM2M_ENDPOINT, &device_obj_conf,
                        &server_inst, &security_inst)) {
        avs_log(setup, ERROR, "event_loop_init error");
    }

    if (temperature_object_add(&event_loop.dm)) {
        avs_log(setup, ERROR, "temperature_object error");
    }
}

void loop() {
    delay(50);
    event_loop_run(&event_loop);
    device_object_reboot_if_needed();

#ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
#endif // LED_BUILTIN
}
