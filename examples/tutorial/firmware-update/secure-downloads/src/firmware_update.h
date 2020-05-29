#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H
#include <anjay/anjay.h>
#include <anjay/fw_update.h>

/**
 * Buffer for the endpoint name that will be used when re-launching the client
 * after firmware upgrade.
 */
extern const char *ENDPOINT_NAME;

/**
 * Installs the firmware update module.
 *
 * @returns 0 on success, negative value otherwise.
 */
int fw_update_install(anjay_t *anjay);

#endif // FIRMWARE_UPDATE_H
