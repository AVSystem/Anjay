#define _DEFAULT_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <anj/sdm_device_object.h>
#include <anj/sdm_impl.h>
#include <anj/sdm_io.h>
#include <anj/sdm_security_object.h>
#include <anj/sdm_server_object.h>

#include "event_loop.h"
#include "example_config.h"
#include "firmware_update.h"

static sdm_device_object_init_t device_obj_conf = {
    .firmware_version = "0.1",
    .supported_binding_modes = "U"
};

static sdm_server_instance_init_t server_inst = {
    .ssid = 1,
    .lifetime = 50,
    .binding = "U",
    .bootstrap_on_registration_failure = &(bool) { false }
};

#ifdef EXAMPLE_WITH_DTLS_PSK
static const char PSK_IDENTITY[] = "identity";
static const char PSK_KEY[] = "P4s$w0rd";
#endif // EXAMPLE_WITH_DTLS_PSK

static sdm_security_instance_init_t security_inst = {
    .ssid = 1,
#ifdef EXAMPLE_WITH_DTLS_PSK
    .server_uri = "coaps://eu.iot.avsystem.cloud:5684",
    .security_mode = SDM_SECURITY_PSK,
    .public_key_or_identity = PSK_IDENTITY,
    .public_key_or_identity_size = sizeof(PSK_IDENTITY) - 1,
    .secret_key = PSK_KEY,
    .secret_key_size = sizeof(PSK_KEY) - 1
#else  // EXAMPLE_WITH_DTLS_PSK
    .server_uri = "coap://eu.iot.avsystem.cloud:5683",
    .security_mode = SDM_SECURITY_NOSEC
#endif // EXAMPLE_WITH_DTLS_PSK
};

static event_loop_ctx_t event_loop;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("No endpoint name given\n");
        return -1;
    }
    // Initialize event_loop structure, install data model with three basic
    // objects
    if (event_loop_init(&event_loop, argv[1], &device_obj_conf, &server_inst,
                        &security_inst)) {
        return -1;
    }

    if (fw_update_object_install(&event_loop.dm,
                                 device_obj_conf.firmware_version, argv[1])) {
        printf("firmware update object installation error\n");
        return -1;
    }

    while (true) {
        event_loop_run(&event_loop);
        fw_process();
        usleep(50 * 1000);
    }
    return 0;
}
