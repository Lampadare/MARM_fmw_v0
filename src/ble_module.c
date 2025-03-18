// Marmoset FMW V0
// ble_module.c

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/sys/reboot.h>

#include "../inc/ble_module.h"
#include "../inc/device_status.h"
#include "../inc/fifo_buffer.h"
#include "../inc/neural_data.h"
#include "../inc/neuralbs.h"

// Log module for BLE
LOG_MODULE_REGISTER(BLE_MODULE, LOG_LEVEL_INF);

extern fifo_buffer_t fifo_buffer;
extern DeviceStatus device_status;

// Advertisement parameters and data
static const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
    (BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_IDENTITY),
    40,  /* Minimum advertising interval (40 * 0.625ms = 25ms) */
    801, /* Maximum advertising interval (801 * 0.625ms ≈ 500ms) */
    NULL);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NBS_VAL),
};

// Global BLE connection pointer and semaphore for connection synchronization
static struct bt_conn *my_conn = NULL;
static K_SEM_DEFINE(ble_conn_sem, 0, 1);

// GATT exchange parameters
static struct bt_gatt_exchange_params exchange_params;
static void exchange_func(struct bt_conn *conn, uint8_t att_err, struct bt_gatt_exchange_params *params);

K_THREAD_STACK_DEFINE(neural_data_notify_stack, NEURAL_DATA_NOTIFY_STACK_SIZE);
K_THREAD_STACK_DEFINE(status_notify_stack, SYSTEM_STATUS_NOTIFY_STACK_SIZE);

static struct k_thread neural_data_notify_thread_data;
static struct k_thread status_notify_thread_data;

// Control flag for neural data notifications
static bool neural_data_emission_enabled = true;
static bool ble_threads_created = false;

// ================================================================================================================================
// ------------------- BLE PHY, Data Length, and MTU Update Functions -------------------
// ================================================================================================================================

static void update_phy(struct bt_conn *conn)
{
    int err;
    const struct bt_conn_le_phy_param preferred_phy = {
        .options = BT_CONN_LE_PHY_OPT_NONE,
        .pref_rx_phy = BT_GAP_LE_PHY_2M,
        .pref_tx_phy = BT_GAP_LE_PHY_2M,
    };
    err = bt_conn_le_phy_update(conn, &preferred_phy);
    if (err)
    {
        LOG_ERR("bt_conn_le_phy_update() returned %d", err);
    }
}

static void update_data_length(struct bt_conn *conn)
{
    int err;
    struct bt_conn_le_data_len_param my_data_len = {
        .tx_max_len = BT_GAP_DATA_LEN_MAX,
        .tx_max_time = BT_GAP_DATA_TIME_MAX,
    };
    err = bt_conn_le_data_len_update(conn, &my_data_len);
    if (err)
    {
        LOG_ERR("data_len_update failed (err %d)", err);
    }
}

static void update_mtu(struct bt_conn *conn)
{
    int err;
    exchange_params.func = exchange_func;

    err = bt_gatt_exchange_mtu(conn, &exchange_params);
    if (err)
    {
        LOG_ERR("bt_gatt_exchange_mtu failed (err %d)", err);
    }
}

// ================================================================================================================================
// ------------------- BLE Connection Callbacks -------------------
// ================================================================================================================================

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        printk("Connection failed (err %u)\n", err);
        return;
    }

    my_conn = bt_conn_ref(conn);

    // Retrieve and log connection parameters
    struct bt_conn_info info;
    err = bt_conn_get_info(conn, &info);
    if (err)
    {
        LOG_ERR("bt_conn_get_info() returned %d", err);
        return;
    }

    double connection_interval = info.le.interval * 1.25; // in ms
    uint16_t supervision_timeout = info.le.timeout * 10;  // in ms
    LOG_INF("Connection parameters: interval %.2f ms, latency %d intervals, timeout %d ms",
            connection_interval, info.le.latency, supervision_timeout);

    update_phy(my_conn);
    update_data_length(my_conn);
    update_mtu(my_conn);

    LOG_INF("Connected");

    // Signal that a connection has been established
    k_sem_give(&ble_conn_sem);

    // Start or resume the BLE module threads
    if (!ble_threads_created)
    {
        ble_start_threads(&fifo_buffer, &device_status);
        ble_threads_created = true;
    }
    else
    {
        // Resume the threads if they were suspended
        k_thread_resume(&neural_data_notify_thread_data);
        k_thread_resume(&status_notify_thread_data);
        neural_data_emission_enabled = true;
        LOG_INF("BLE threads resumed");
    }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason %u)", reason);
    if (my_conn == conn)
    {
        bt_conn_unref(my_conn);
        my_conn = NULL;
    }

    // Suspend the BLE module threads
    if (ble_threads_created)
    {
        k_thread_suspend(&neural_data_notify_thread_data);
        k_thread_suspend(&status_notify_thread_data);
        neural_data_emission_enabled = false;
        LOG_INF("BLE threads suspended");
    }
    else
    {
        LOG_ERR("BLE threads not created, cannot suspend, rebooting...");
        sys_reboot(SYS_REBOOT_COLD);
    }

    // Reconnect to the BLE module
    reconnect_ble();
}

static void on_le_param_updated(struct bt_conn *conn, uint16_t interval,
                                uint16_t latency, uint16_t timeout)
{
    double connection_interval = interval * 1.25; // in ms
    uint16_t supervision_timeout = timeout * 10;  // in ms
    LOG_INF("Connection parameters updated: interval %.2f ms, latency %d intervals, timeout %d ms",
            connection_interval, latency, supervision_timeout);
}

static void on_le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
    if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_1M)
    {
        LOG_INF("PHY updated. New PHY: 1M");
    }
    else if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_2M)
    {
        LOG_INF("PHY updated. New PHY: 2M");
    }
    else if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_CODED_S8)
    {
        LOG_INF("PHY updated. New PHY: Long Range");
    }
}

static void on_le_data_len_updated(struct bt_conn *conn,
                                   struct bt_conn_le_data_len_info *info)
{
    uint16_t tx_len = info->tx_max_len;
    uint16_t tx_time = info->tx_max_time;
    uint16_t rx_len = info->rx_max_len;
    uint16_t rx_time = info->rx_max_time;
    LOG_INF("Data length updated. Length %d/%d bytes, time %d/%d us",
            tx_len, rx_len, tx_time, rx_time);
}

static struct bt_conn_cb connection_callbacks = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .le_param_updated = on_le_param_updated,
    .le_phy_updated = on_le_phy_updated,
    .le_data_len_updated = on_le_data_len_updated,
};

// ================================================================================================================================
// ------------------- MTU Exchange Callback -------------------
// ================================================================================================================================

static void exchange_func(struct bt_conn *conn, uint8_t att_err,
                          struct bt_gatt_exchange_params *params)
{
    LOG_INF("MTU exchange %s", att_err == 0 ? "successful" : "failed");
    if (att_err == 0)
    {
        uint16_t payload_mtu = bt_gatt_get_mtu(conn) - 3; // subtract header bytes
        LOG_INF("New MTU: %d bytes", payload_mtu);
    }
}

// ================================================================================================================================
// ------------------- BLE Notification Threads -------------------
// ================================================================================================================================

/* Status notification thread:
 * The device status pointer is passed as the first argument (p1).
 */
static void status_notify_thread(void *device_status_ptr, void *p2, void *p3)
{
    DeviceStatus *status = (DeviceStatus *)device_status_ptr;
    while (1)
    {
        if (status)
        {
            nbs_send_system_status_notify(status);
        }
        else
        {
            LOG_ERR("Device status pointer is NULL");
        }
        k_sleep(K_SECONDS(SYSTEM_STATUS_NOTIFY_INTERVAL));
    }
}

/* Neural data notification thread:
 * The FIFO buffer pointer is passed as the first argument (p1).
 */
static void neural_data_notify_thread(void *fifo_ptr, void *p2, void *p3)
{
    fifo_buffer_t *fifo = (fifo_buffer_t *)fifo_ptr;
    NeuralData samples[MAX_NEURAL_SAMPLES_PER_BLE];
    size_t count = 0;

    if (!fifo)
    {
        LOG_ERR("FIFO buffer pointer is NULL. Exiting neural data notify thread.");
        return;
    }

    while (1)
    {
        /* Check if emission is enabled */
        if (!neural_data_emission_enabled)
        {
            k_sleep(K_MSEC(NEURAL_DATA_NOTIFY_INTERVAL));
            continue;
        }

        // Wait until BLE data is available in the FIFO
        int ret = k_sem_take(&fifo->ble_data_available, K_NO_WAIT);
        if (ret != 0)
        {
            k_sleep(K_MSEC(NEURAL_DATA_NOTIFY_INTERVAL));
            continue;
        }

        /* Read up to MAX_NEURAL_SAMPLES_PER_BLE samples from the FIFO */
        count = read_from_fifo_buffer_ble(fifo, samples, MAX_NEURAL_SAMPLES_PER_BLE);
        nbs_send_neural_data_notify(samples, count);
        k_sleep(K_MSEC(NEURAL_DATA_NOTIFY_INTERVAL));
    }
}

// ================================================================================================================================
// ------------------- Public API Functions -------------------
// ================================================================================================================================

int ble_init(void)
{
    int err;

    /* Initialize the Bluetooth stack */
    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }
    LOG_INF("Bluetooth initialized");

    /* Register BLE connection callbacks */
    bt_conn_cb_register(&connection_callbacks);

    /* Start advertising */
    err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return err;
    }
    LOG_INF("Advertising successfully started");
    return 0;
}

int ble_wait_for_connection(k_timeout_t timeout)
{
    /* Wait for the on_connected callback to signal a connection */
    int err = k_sem_take(&ble_conn_sem, timeout);
    if (err)
    {
        LOG_ERR("Failed to establish BLE connection within timeout");
        return err;
    }
    return 0;
}

struct bt_conn *ble_get_current_connection(void)
{
    return my_conn;
}

void reconnect_ble(void)
{
    int err;
    // Stop advertising first, if fails, reboot
    err = bt_le_adv_stop();
    if (err)
    {
        LOG_ERR("Failed to stop advertising (err %d)", err);
        sys_reboot(SYS_REBOOT_COLD);
    }
    // Restart advertising, if fails, reboot
    err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("Failed to restart advertising (err %d), rebooting...", err);
        sys_reboot(SYS_REBOOT_COLD);
    }
    // Wait for connection, if fails, reboot
    err = ble_wait_for_connection(K_SECONDS(60));
    if (err)
    {
        LOG_ERR("Failed to reconnect to BLE module, rebooting...");
        sys_reboot(SYS_REBOOT_COLD);
    }
    else
    {
        LOG_INF("BLE module reconnected!");
    }
}

/**
 * @brief Starts the BLE notification threads.
 *
 * @param fifo Pointer to the FIFO buffer for neural data.
 * @param status Pointer to the device status structure.
 */
void ble_start_threads(fifo_buffer_t *fifo, DeviceStatus *status)
{
    /* Create the neural data notification thread, passing the FIFO pointer */
    k_thread_create(&neural_data_notify_thread_data, neural_data_notify_stack,
                    K_THREAD_STACK_SIZEOF(neural_data_notify_stack),
                    neural_data_notify_thread, fifo, NULL, NULL,
                    BLE_NEURAL_DATA_NOTIFY_PRIORITY, 0, K_MSEC(500));
    LOG_INF("Neural data notify thread created");

    /* Create the system status notification thread, passing the device status pointer */
    k_thread_create(&status_notify_thread_data, status_notify_stack,
                    K_THREAD_STACK_SIZEOF(status_notify_stack),
                    status_notify_thread, status, NULL, NULL,
                    BLE_STATUS_NOTIFY_PRIORITY, 0, K_MSEC(1000));
    LOG_INF("Status notify thread created");
}
