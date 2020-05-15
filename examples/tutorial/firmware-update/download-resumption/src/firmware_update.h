#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H
#include <anjay/anjay.h>
#include <anjay/fw_update.h>

/**
 * Installs the firmware update module.
 *
 * @returns 0 on success, negative value otherwise.
 */
int fw_update_install(anjay_t *anjay);

#endif // FIRMWARE_UPDATE_H
