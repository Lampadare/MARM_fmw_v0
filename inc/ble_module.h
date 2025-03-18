// Marmoset FMW V0
// ble_module.h

#ifndef BLE_MODULE_H_
#define BLE_MODULE_H_

#include <zephyr/kernel.h>
#include "../inc/device_status.h"
#include "../inc/fifo_buffer.h"

// Define thread stacks
#define NEURAL_DATA_NOTIFY_STACK_SIZE 8192
#define SYSTEM_STATUS_NOTIFY_STACK_SIZE 8192

/* BLE Payload and sample limits */
#define BLE_PAYLOAD_MAX 244
#define MAX_NEURAL_SAMPLES_PER_BLE 6

/* BLE thread priorities and intervals */
#define BLE_STATUS_NOTIFY_PRIORITY 8
#define BLE_NEURAL_DATA_NOTIFY_PRIORITY 4

#define SYSTEM_STATUS_NOTIFY_INTERVAL 1 // seconds
#define NEURAL_DATA_NOTIFY_INTERVAL 1   // milliseconds

/* Device name definitions (using Zephyr's CONFIG_BT_DEVICE_NAME) */
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/**
 * @brief Initializes the BLE stack and starts advertising.
 *
 * @return 0 on success or a negative error code.
 */
int ble_init(void);

/**
 * @brief Wait for a BLE connection to be established.
 *
 * @param timeout Timeout for waiting.
 * @return 0 on success or a negative error code.
 */
int ble_wait_for_connection(k_timeout_t timeout);

/**
 * @brief Get the current active BLE connection.
 *
 * @return Pointer to the current BLE connection.
 */
struct bt_conn *ble_get_current_connection(void);

/**
 * @brief Restart BLE advertising (used on reconnect).
 */
void reconnect_ble(void);

/**
 * @brief Start BLE notification threads.
 *
 * @param fifo Pointer to the FIFO buffer for neural data.
 * @param status Pointer to the device status structure.
 */
void ble_start_threads(fifo_buffer_t *fifo, DeviceStatus *status);

/**
 * @brief Pause BLE neural data emission.
 */
void ble_pause_neuraldata_emission(void);

/**
 * @brief Resume BLE neural data emission.
 */
void ble_resume_neuraldata_emission(void);

#endif /* BLE_MODULE_H_ */