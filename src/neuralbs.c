/** @file
 *  @brief Neural Bluetooth Service (NBS) sample
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "../inc/neuralbs.h"
#include "../inc/neural_data.h"
#include "../inc/device_status.h"

LOG_MODULE_DECLARE(Neural_Bluetooth_Service);

static bool notify_neural_data_enabled;
static bool notify_device_status_enabled;

static ssize_t read_neural_data(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset)
{
    const char *value = (const char *)attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
}

/* Implement the configuration change callback function for device status characteristic */
static void nbs_neural_data_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                            uint16_t value)
{
    notify_neural_data_enabled = (value == BT_GATT_CCC_NOTIFY);
}

/* Implement the configuration change callback function for device status characteristic */
static void nbs_status_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                       uint16_t value)
{
    notify_device_status_enabled = (value == BT_GATT_CCC_NOTIFY);
}

/* LED Button Service Declaration */
BT_GATT_SERVICE_DEFINE(
    my_lbs_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_NBS),

    BT_GATT_CHARACTERISTIC(
        BT_UUID_NBS_NEURAL_DATA,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ | BT_GATT_PERM_NONE,
        read_neural_data,
        NULL,
        &latest_neural_data),

    BT_GATT_CCC(nbs_neural_data_ccc_cfg_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    BT_GATT_CHARACTERISTIC(
        BT_UUID_NBS_DEVICE_STATUS,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL,
        NULL,
        NULL),

    BT_GATT_CCC(nbs_status_ccc_cfg_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

/* Send notifications for the neural data characteristic */
int nbs_send_neural_data_notify(NeuralData *latest_neural_data)
{
    if (!notify_device_status_enabled)
    {
        return -EACCES;
    }

    return bt_gatt_notify(NULL, &my_lbs_svc.attrs[1],
                          latest_neural_data,
                          sizeof(*latest_neural_data));
}

/* Send notifications for the system status characteristic */
// TODO: later implement a function to update the device status
int nbs_send_system_status_notify(DeviceStatus *device_status)
{
    if (!notify_device_status_enabled)
    {
        return -EACCES;
    }

    return bt_gatt_notify(NULL, &my_lbs_svc.attrs[4],
                          device_status,
                          sizeof(*device_status));
}