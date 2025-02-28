#ifndef TEMPERATURE_OBJECT_H
#define TEMPERATURE_OBJECT_H

#include <anjay/dm.h>

#include "gateway_server.h"

const anjay_dm_object_def_t **
temperature_object_create(int id, gateway_srv_t *gateway_srv);
void temperature_object_release(const anjay_dm_object_def_t **def);
void temperature_object_send(anjay_t *anjay, AVS_LIST(end_device_t) end_device);
void temperature_object_update_value(anjay_t *anjay,
                                     const anjay_dm_object_def_t **def);
void temperature_object_evaluation_period_update_value(
        anjay_t *anjay,
        const anjay_dm_object_def_t **def,
        int32_t *evaluation_period);
#endif // TEMPERATURE_OBJECT_H
