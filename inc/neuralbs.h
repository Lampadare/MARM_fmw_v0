#ifndef BT_NBS_H_
#define BT_NBS_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <zephyr/types.h>
#include "device_status.h"
#include "neural_data.h"

/** @brief NBS Service UUID. */
#define BT_UUID_NBS_VAL BT_UUID_128_ENCODE(0xac9a900b, 0xd5c2, 0x4eea, 0xa18b, 0xc30efc00d25e)

/** @brief Neural Data Characteristic UUID. */
#define BT_UUID_NBS_NEURAL_DATA_VAL \
    BT_UUID_128_ENCODE(0xbcd5243f, 0x0607, 0x4899, 0xafda, 0x999999999999)

/** @brief Device Status Characteristic UUID. */
#define BT_UUID_NBS_DEVICE_STATUS_VAL BT_UUID_128_ENCODE(0xd3171a00, 0x57e9, 0x476d, 0xa6db, 0x111111111111)

#define BT_UUID_NBS BT_UUID_DECLARE_128(BT_UUID_NBS_VAL)
#define BT_UUID_NBS_NEURAL_DATA BT_UUID_DECLARE_128(BT_UUID_NBS_NEURAL_DATA_VAL)
#define BT_UUID_NBS_DEVICE_STATUS BT_UUID_DECLARE_128(BT_UUID_NBS_DEVICE_STATUS_VAL)

    int nbs_send_neural_data_notify(NeuralData *latest_neural_data);
    int nbs_send_system_status_notify(DeviceStatus *device_status);

#ifdef __cplusplus
}
#endif

#endif /* BT_NBS_H_ */