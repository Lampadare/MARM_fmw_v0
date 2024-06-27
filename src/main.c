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

static struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
	(BT_LE_ADV_OPT_CONNECTABLE |
	 BT_LE_ADV_OPT_USE_IDENTITY), /* Connectable advertising and use identity address */
	800,						  /* Min Advertising Interval 500ms (800*0.625ms) */
	801,						  /* Max Advertising Interval 500.625ms (801*0.625ms) */
	NULL);						  /* Set to NULL for undirected advertising */

LOG_MODULE_REGISTER(Marmoset_FMW, LOG_LEVEL_INF);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define STACKSIZE 1024
#define STATUS_NOTIFY_PRIORITY 7
#define NEURAL_DATA_NOTIFY_PRIORITY 4
#define SYSTEM_STATUS_NOTIFY_INTERVAL 1 // system status notify interval in seconds
#define NEURAL_DATA_INTERVAL 15			// neural data interval in milliseconds
#define NEURAL_DATA_NOTIFY_INTERVAL 15	// neural data notify interval in milliseconds

struct bt_conn *my_conn = NULL;
static struct bt_gatt_exchange_params exchange_params;
static void exchange_func(struct bt_conn *conn, uint8_t att_err, struct bt_gatt_exchange_params *params);

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

// Function to get the current time in milliseconds since the start of the 12-hour period
uint32_t get_current_timestamp()
{
	int64_t uptime_ms = k_uptime_get();
	uint32_t timestamp = (uint32_t)(uptime_ms % (12 * 60 * 60 * 1000)); // 12 hours in milliseconds
	return timestamp;
}

// Function to update the neural data
void update_neural_data()
{
	for (int i = 0; i < MAX_CHANNELS; i++)
	{
		for (int j = 0; j < MAX_BYTES_PER_CHANNEL; j++)
		{
			latest_neural_data.channels[i].data[j]++;
			if (latest_neural_data.channels[i].data[j] == 200)
			{
				latest_neural_data.channels[i].data[j] = 0;
			}
		}
	}
	latest_neural_data.timestamp = get_current_timestamp();
}

void status_notify_thread(void)
{
	while (1)
	{
		nbs_send_system_status_notify(&device_status);
		k_sleep(K_SECONDS(SYSTEM_STATUS_NOTIFY_INTERVAL));
	}
}

void neural_data_notify_thread(void)
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

	for (;;)
	{
		update_neural_data();
		k_sleep(K_MSEC(NEURAL_DATA_INTERVAL));
	}
}

/* STEP 18.2 - Define and initialize a thread to send data periodically */
K_THREAD_DEFINE(neural_data_notify_thread_id, STACKSIZE, neural_data_notify_thread, NULL, NULL,
				NULL, NEURAL_DATA_NOTIFY_PRIORITY, 0, 0);

/* STEP 18.2 - Define and initialize a thread to send data periodically */
K_THREAD_DEFINE(status_notify_thread_id, STACKSIZE, status_notify_thread, NULL, NULL,
				NULL, STATUS_NOTIFY_PRIORITY, 0, 0);
