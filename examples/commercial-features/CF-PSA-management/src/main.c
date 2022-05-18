#include <avsystem/commons/avs_crypto_pki.h>
#include <avsystem/commons/avs_crypto_psk.h>
#include <avsystem/commons/avs_log.h>

const char *USAGE_STR =
        "\nusage: %s COMMAND TYPE ID [PATH || DATA]\n"
        "\tCOMMAND:\tstore|remove\n"
        "\tTYPE:\t\tpkey|certificate|psk_key|psk_identity\n"
        "\tID:\t\tPSA ID of the considered credential\n"
        "\tPATH:\t\tpath to the credential to be stored (4th argument is "
        "to be a path when storing PKI credentials)\n"
        "\tDATA:\t\tcredential to be stored (4th argument is considered to be "
        "credential itself when storing PSK credential)";

int main(int argc, char *argv[]) {
    int result = 0;

    if (argc < 4 || argc > 5
            || ((strcmp(argv[1], "remove") || argc != 4)
                && (strcmp(argv[1], "store") || argc != 5))
            || (strcmp(argv[2], "pkey") && strcmp(argv[2], "certificate")
                && strcmp(argv[2], "psk_key")
                && strcmp(argv[2], "psk_identity"))) {
        avs_log(tutorial, INFO, USAGE_STR, argv[0]);
        return -1;
    }

    char query[sizeof("kid=0x") + 9];
    sprintf(query, "kid=%#010x", atoi(argv[3]));

    if (!strcmp(argv[1], "remove")) {
        if (!strcmp(argv[2], "pkey")) {
            if (avs_is_err(avs_crypto_pki_engine_key_rm(query))) {
                avs_log(tutorial, ERROR, "Private key removal failed");
                return -1;
            }
        } else if (!strcmp(argv[2], "certificate")) {
            if (avs_is_err(avs_crypto_pki_engine_certificate_rm(query))) {
                avs_log(tutorial, ERROR, "Certificate removal failed");
                return -1;
            }
        } else if (!strcmp(argv[2], "psk_key")) {
            if (avs_is_err(avs_crypto_psk_engine_key_rm(query))) {
                avs_log(tutorial, ERROR, "PSK key removal failed");
                return -1;
            }
        } else {
            if (avs_is_err(avs_crypto_psk_engine_identity_rm(query))) {
                avs_log(tutorial, ERROR, "PSK identity removal failed");
                return -1;
            }
        }
    } else {
        if (!strcmp(argv[2], "pkey")) {
            avs_crypto_private_key_info_t key_info =
                    avs_crypto_private_key_info_from_file(argv[4], NULL);
            if (avs_is_err(avs_crypto_pki_engine_key_store(
                        query, &key_info, NULL))) {
                avs_log(tutorial, ERROR, "Storing private key failed");
                return -1;
            }
        } else if (!strcmp(argv[2], "certificate")) {
            avs_crypto_certificate_chain_info_t cert_info =
                    avs_crypto_certificate_chain_info_from_file(argv[4]);
            if (avs_is_err(avs_crypto_pki_engine_certificate_store(
                        query, &cert_info))) {
                avs_log(tutorial, ERROR, "Storing certificate failed");
                return -1;
            }
        } else if (!strcmp(argv[2], "psk_key")) {
            avs_crypto_psk_key_info_t psk_key_info =
                    avs_crypto_psk_key_info_from_buffer(argv[4],
                                                        strlen(argv[4]));
            if (avs_is_err(avs_crypto_psk_engine_key_store(query,
                                                           &psk_key_info))) {
                avs_log(tutorial, ERROR, "Storing PSK key failed");
                return -1;
            }
        } else {
            avs_crypto_psk_identity_info_t identity_info =
                    avs_crypto_psk_identity_info_from_buffer(argv[4],
                                                             strlen(argv[4]));
            if (avs_is_err(avs_crypto_psk_engine_identity_store(
                        query, &identity_info))) {
                avs_log(tutorial, ERROR, "Storing PSK identity failed");
                return -1;
            }
        }
    }

    return 0;
}
