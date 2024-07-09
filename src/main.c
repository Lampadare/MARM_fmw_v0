#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>

#include "../inc/neuralbs.h"
#include "../inc/device_status.h"
#include "../inc/neural_data.h"
#include "../inc/fifo_buffer.h"
#include "../inc/fakedata_module.h"
#include "../inc/sd_card.h"

static struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
	(BT_LE_ADV_OPT_CONNECTABLE |
	 BT_LE_ADV_OPT_USE_IDENTITY), /* Connectable advertising and use identity address */
	800,						  /* Min Advertising Interval 500ms (800*0.625ms) */
	801,						  /* Max Advertising Interval 500.625ms (801*0.625ms) */
	NULL);						  /* Set to NULL for undirected advertising */

LOG_MODULE_REGISTER(Marmoset_FMW, LOG_LEVEL_INF);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define STATUS_NOTIFY_PRIORITY 8
#define SD_CARD_THREAD_PRIORITY 2
#define NEURAL_DATA_NOTIFY_PRIORITY 4
#define FAKEDATA_THREAD_PRIORITY 3

#define NEURAL_DATA_NOTIFY_STACK_SIZE 8192
#define SYSTEM_STATUS_NOTIFY_STACK_SIZE 8192

#define SYSTEM_STATUS_NOTIFY_INTERVAL 1 // system status notify interval in seconds
#define NEURAL_DATA_NOTIFY_INTERVAL 4	// neural data notify interval in milliseconds

// Define thread stacks
K_THREAD_STACK_DEFINE(neural_data_notify_stack, NEURAL_DATA_NOTIFY_STACK_SIZE);
K_THREAD_STACK_DEFINE(status_notify_stack, SYSTEM_STATUS_NOTIFY_STACK_SIZE);

// Declare thread IDs
static struct k_thread neural_data_notify_thread_data;
static struct k_thread status_notify_thread_data;

struct bt_conn *my_conn = NULL;
static struct bt_gatt_exchange_params exchange_params;
static void exchange_func(struct bt_conn *conn, uint8_t att_err, struct bt_gatt_exchange_params *params);

static fifo_buffer_t fifo_buffer;

/* Fake latest neural data */
NeuralData latest_neural_data = {
	.channels = {{{0}}}, // Initialize all channels to zero
	.timestamp = 0};

// Define and initialize the device status with default values
DeviceStatus device_status = {
	.battery_level = 100,
	.temperature = 25,
	.recording_status = true,
	.configuration = "v0.0.1"};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NBS_VAL),
};

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
	err = bt_conn_le_data_len_update(my_conn, &my_data_len);
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

void status_notify_thread(void *p1, void *p2, void *p3)
{
	while (1)
	{
		nbs_send_system_status_notify(&device_status);
		k_sleep(K_SECONDS(SYSTEM_STATUS_NOTIFY_INTERVAL));
	}
}

void neural_data_notify_thread(void *p1, void *p2, void *p3)
{
	while (1)
	{
		nbs_send_neural_data_notify(&latest_neural_data);
		k_sleep(K_MSEC(NEURAL_DATA_NOTIFY_INTERVAL));
	}
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err)
	{
		printk("Connection failed (err %u)\n", err);
		return;
	}

	my_conn = bt_conn_ref(conn);
	struct bt_conn_info info;
	err = bt_conn_get_info(conn, &info);
	if (err)
	{
		LOG_ERR("bt_conn_get_info() returned %d", err);
		return;
	}

	double connection_interval = info.le.interval * 1.25; // in ms
	uint16_t supervision_timeout = info.le.timeout * 10;  // in ms
	LOG_INF("Connection parameters: interval %.2f ms, latency %d intervals, timeout %d ms", connection_interval, info.le.latency, supervision_timeout);

	update_phy(my_conn);
	update_data_length(my_conn);
	update_mtu(my_conn);

	printk("Connected\n");
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason %u)\n", reason);
}

void on_le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
	double connection_interval = interval * 1.25; // in ms
	uint16_t supervision_timeout = timeout * 10;  // in ms
	LOG_INF("Connection parameters updated: interval %.2f ms, latency %d intervals, timeout %d ms", connection_interval, latency, supervision_timeout);
}

void on_le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
	// PHY Updated
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

void on_le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info)
{
	uint16_t tx_len = info->tx_max_len;
	uint16_t tx_time = info->tx_max_time;
	uint16_t rx_len = info->rx_max_len;
	uint16_t rx_time = info->rx_max_time;
	LOG_INF("Data length updated. Length %d/%d bytes, time %d/%d us", tx_len, rx_len, tx_time, rx_time);
}

static void exchange_func(struct bt_conn *conn, uint8_t att_err,
						  struct bt_gatt_exchange_params *params)
{
	LOG_INF("MTU exchange %s", att_err == 0 ? "successful" : "failed");
	if (!att_err)
	{
		uint16_t payload_mtu = bt_gatt_get_mtu(conn) - 3; // 3 bytes used for Attribute headers.
		LOG_INF("New MTU: %d bytes", payload_mtu);
	}
}

struct bt_conn_cb connection_callbacks = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.le_param_updated = on_le_param_updated,
	.le_phy_updated = on_le_phy_updated,
	.le_data_len_updated = on_le_data_len_updated,
};

int main(void)
{
	int err;

	LOG_INF("Marmoset FMW V0 \n");

	err = bt_enable(NULL);
	if (err)
	{
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		return -1;
	}
	bt_conn_cb_register(&connection_callbacks);

	LOG_INF("Bluetooth initialized\n");
	err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err)
	{
		LOG_ERR("Advertising failed to start (err %d)\n", err);
		return -1;
	}

	LOG_INF("Advertising successfully started\n");

	LOG_INF("Initializing FIFO buffer...");
	init_fifo_buffer(&fifo_buffer);

	LOG_INF("Initializing SD card...");
	err = sd_card_init();
	if (err)
	{
		LOG_ERR("SD card initialization failed (err %d)\n", err);
		return -1;
	}

	LOG_INF("=======!!! All systems initialized !!!======= \n");

	k_sleep(K_MSEC(100));

	// Create threads dynamically
	LOG_INF("Creating neural data notify thread...");
	k_thread_create(&neural_data_notify_thread_data, neural_data_notify_stack,
					K_THREAD_STACK_SIZEOF(neural_data_notify_stack),
					neural_data_notify_thread, NULL, NULL, NULL,
					NEURAL_DATA_NOTIFY_PRIORITY, 0, K_MSEC(1000));
	LOG_INF("Neural data notify thread created");

	LOG_INF("Creating status notify thread...");
	k_thread_create(&status_notify_thread_data, status_notify_stack,
					K_THREAD_STACK_SIZEOF(status_notify_stack),
					status_notify_thread, NULL, NULL, NULL,
					STATUS_NOTIFY_PRIORITY, 0, K_MSEC(3000));
	LOG_INF("Status notify thread created");

	LOG_INF("Creating fakedata thread...");
	k_thread_create(&fakedata_thread_data, fakedata_stack,
					FAKEDATA_THREAD_STACK_SIZE,
					fakedata_thread, &fifo_buffer, NULL, NULL,
					FAKEDATA_THREAD_PRIORITY, 0, K_MSEC(10000));
	LOG_INF("Fakedata thread created");

	LOG_INF("Creating SD card writer thread...");
	k_thread_create(&sd_card_thread_data, sd_card_stack,
					SD_CARD_THREAD_STACK_SIZE,
					sd_card_writer_thread, &fifo_buffer, NULL, NULL,
					SD_CARD_THREAD_PRIORITY, 0, K_MSEC(10400));
	LOG_INF("SD card writer thread created");

	LOG_INF("=======!!! All threads created successfully !!!======= \n");

	return 0;
}
