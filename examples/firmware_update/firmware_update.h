#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H

#include <anj/sdm_io.h>

void fw_process(void);
int fw_update_object_install(sdm_data_model_t *dm,
                             const char *firmware_version,
                             const char *endpoint_name);

#endif // FIRMWARE_UPDATE_H
