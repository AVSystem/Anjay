#include <anjay/anjay.h>
#include <anjay/at_sms.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/avs_log.h>

// Installs Security Object and adds and instance of it.
// An instance of Security Object provides information needed to connect to
// LwM2M server.
static int setup_security_object(anjay_t *anjay) {
    if (anjay_security_object_install(anjay)) {
        return -1;
    }

    static const char PSK_IDENTITY[] = "identity";
    static const char PSK_KEY[] = "P4s$w0rd";

    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "tel:+12125550178",
        .security_mode = ANJAY_SECURITY_NOSEC,
        .sms_security_mode = ANJAY_SMS_SECURITY_DTLS_PSK,
        .sms_key_parameters = (const uint8_t *) PSK_IDENTITY,
        .sms_key_parameters_size = strlen(PSK_IDENTITY),
        .sms_secret_key = (const uint8_t *) PSK_KEY,
        .sms_secret_key_size = strlen(PSK_KEY),
        .server_name_indication = "eu.iot.avsystem.cloud"
    };

    // Anjay will assign Instance ID automatically
    anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
    if (anjay_security_object_add_instance(anjay, &security_instance,
                                           &security_instance_id)) {
        return -1;
    }

    return 0;
}

// Installs Server Object and adds and instance of it.
// An instance of Server Object provides the data related to a LwM2M Server.
static int setup_server_object(anjay_t *anjay) {
    if (anjay_server_object_install(anjay)) {
        return -1;
    }

    const anjay_server_instance_t server_instance = {
        // Server Short ID
        .ssid = 1,
        // Client will send Update message often than every 60 seconds
        .lifetime = 60,
        // Disable Default Minimum Period resource
        .default_min_period = -1,
        // Disable Default Maximum Period resource
        .default_max_period = -1,
        // Disable Disable Timeout resource
        .disable_timeout = -1,
        // Sets preferred transport to SMS
        .binding = "S"
    };

    // Anjay will assign Instance ID automatically
    anjay_iid_t server_instance_id = ANJAY_ID_INVALID;
    if (anjay_server_object_add_instance(anjay, &server_instance,
                                         &server_instance_id)) {
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME MODEM_DEVICE",
                argv[0]);
        return -1;
    }

    const anjay_configuration_t CONFIG = {
        .endpoint_name = argv[1],
        .in_buffer_size = 4000,
        .out_buffer_size = 4000,
        .msg_cache_size = 4000,
        .sms_driver = anjay_at_sms_create(argv[2]),
        .local_msisdn = "14155550125",
        .default_tls_ciphersuites = {
            // TLS_PSK_WITH_AES_128_CCM_8
            .ids = (uint32_t[]){ 0xC0A8 },
            .num_ids = 1
        }
    };

    anjay_t *anjay = anjay_new(&CONFIG);
    if (!anjay) {
        avs_log(tutorial, ERROR, "Could not create Anjay object");
        return -1;
    }

    int result = 0;
    // Setup necessary objects
    if (setup_security_object(anjay) || setup_server_object(anjay)) {
        result = -1;
    }

    if (!result) {
        result = anjay_event_loop_run(
                anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));
    }

    anjay_delete(anjay);
    return result;
}
