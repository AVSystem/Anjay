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

#include "bootstrap_event_loop.h"
#include "example_config.h"

static sdm_device_object_init_t device_obj_conf = {
    .firmware_version = "0.1",
    .supported_binding_modes = "U"
};

#ifdef EXAMPLE_WITH_DTLS_PSK
static const char PSK_IDENTITY[] = "identity";
static const char PSK_KEY[] = "P4s$w0rd";
#endif // EXAMPLE_WITH_DTLS_PSK

static sdm_security_instance_init_t bootstrap_security_inst = {
    .bootstrap_server = true,
#ifdef EXAMPLE_WITH_DTLS_PSK
    .server_uri = "coaps://eu.iot.avsystem.cloud:5694",
    .security_mode = SDM_SECURITY_PSK,
    .public_key_or_identity = PSK_IDENTITY,
    .public_key_or_identity_size = sizeof(PSK_IDENTITY) - 1,
    .secret_key = PSK_KEY,
    .secret_key_size = sizeof(PSK_KEY) - 1
#else  // EXAMPLE_WITH_DTLS_PSK
    .server_uri = "coap://eu.iot.avsystem.cloud:5693",
    .security_mode = SDM_SECURITY_NOSEC
#endif // EXAMPLE_WITH_DTLS_PSK
};

static event_loop_ctx_t event_loop;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("No endpoint name given\n");
        return -1;
    }
    // Bootstrap security instance doesn't have related server instance
    if (event_loop_init(&event_loop, argv[1], &device_obj_conf,
                        &bootstrap_security_inst)) {
        return -1;
    }

    while (true) {
        event_loop_run(&event_loop);
        usleep(50 * 1000);
    }
    return 0;
}
