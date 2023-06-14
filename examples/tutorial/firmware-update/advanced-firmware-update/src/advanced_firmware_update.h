#ifndef ADVANCED_FIRMWARE_UPDATE_H
#define ADVANCED_FIRMWARE_UPDATE_H

#include <anjay/advanced_fw_update.h>
#include <anjay/anjay.h>

#include <avsystem/commons/avs_log.h>

#define AFU_DEFAULT_FIRMWARE_VERSION "1.0.0"
#define AFU_ADD_FILE_DEFAULT_CONTENT "1.1.1"

#define AFU_DEFAULT_FIRMWARE_INSTANCE_IID 0
#define AFU_NUMBER_OF_FIRMWARE_INSTANCES 3

/**
 * Buffer for the endpoint name that will be used when re-launching the client
 * after firmware upgrade.
 */
extern const char *ENDPOINT_NAME;

/**
 * Installs the advanced firmware update module.
 *
 * @returns 0 on success, negative value otherwise.
 */
int afu_update_install(anjay_t *anjay);

#endif // ADVANCED_FIRMWARE_UPDATE_H
