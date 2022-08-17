#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/avs_log.h>

#include <signal.h>
#include <time.h>

static anjay_t *volatile g_anjay;

void signal_handler(int signum) {
    if (signum == SIGINT && g_anjay) {
        anjay_event_loop_interrupt(g_anjay);
    }
}

const char HSM_ALPHABET[] = "0123456789abcdef";
const char HSM_TEMPLATE[] = "kid=0x0000....";

static const char *generate_hsm_address(anjay_iid_t iid,
                                        anjay_ssid_t ssid,
                                        const void *data,
                                        size_t data_size,
                                        void *arg) {
    (void) iid;
    (void) ssid;
    (void) data;
    (void) data_size;
    (void) arg;

    static size_t offset = 0ul;
    static char buffer[1024];

    if (offset + sizeof(HSM_TEMPLATE) > sizeof(buffer)) {
        avs_log(tutorial, ERROR, "Wrong HSM address");
        return NULL;
    }

    static avs_rand_seed_t SEED;
    if (!SEED) {
        SEED = (avs_rand_seed_t) time(NULL);
    }

    char *result = buffer + offset;
    offset += sizeof(HSM_TEMPLATE);
    strcpy(result, HSM_TEMPLATE);

    for (int i = 0; result[i]; i++) {
        if (result[i] == '.') {
            result[i] = HSM_ALPHABET[(size_t) avs_rand_r(&SEED)
                                     % (sizeof(HSM_ALPHABET) - 1)];
        }
    }

    return result;
}

static const anjay_security_hsm_configuration_t HSM_CONFIG = {
    .psk_identity_cb = generate_hsm_address,
    .psk_key_cb = generate_hsm_address
};

// Installs Security Object and adds its instance for the bootstrap server.
static int
setup_security_object(anjay_t *anjay, const char *identity, const char *key) {
    if (anjay_security_object_install_with_hsm(anjay, &HSM_CONFIG)) {
        return -1;
    }

    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coaps://eu.iot.avsystem.cloud:5694",
        .bootstrap_server = true,
        .security_mode = ANJAY_SECURITY_PSK,
        .public_cert_or_psk_identity = identity,
        .public_cert_or_psk_identity_size = strlen(identity),
        .private_cert_or_psk_key = key,
        .private_cert_or_psk_key_size = strlen(key)
    };

    // Anjay will assign Instance ID automatically
    anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
    if (anjay_security_object_add_instance(anjay, &security_instance,
                                           &security_instance_id)) {
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int result = 0;

    if (argc != 2) {
        avs_log(tutorial, ERROR, "usage: %s PSK_IDENTITY PSK_KEY", argv[0]);
        avs_log(tutorial, INFO,
                "note: PSK_IDENTITY is used also as an endpoint name");
        return -1;
    }

    signal(SIGINT, signal_handler);

    const anjay_configuration_t config = {
        .endpoint_name = argv[1],
        .in_buffer_size = 4000,
        .out_buffer_size = 4000,
        .msg_cache_size = 4000
    };

    g_anjay = anjay_new(&config);
    if (!g_anjay) {
        avs_log(tutorial, ERROR, "Could not create Anjay object");
        return -1;
    }

    if (setup_security_object(g_anjay, argv[1], argv[2])
            || anjay_server_object_install(g_anjay)) {
        result = -1;
    }

    if (!result) {
        result = anjay_event_loop_run(
                g_anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));
    }

    anjay_delete(g_anjay);

    return result;
}
