// Marmoset FMW V0
// main.c

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/reboot.h>

#include "../inc/neuralbs.h"
#include "../inc/device_status.h"
#include "../inc/neural_data.h"
#include "../inc/fifo_buffer.h"
#include "../inc/fakedata_module.h"
#include "../inc/sd_card.h"
#include "../inc/intan.h"

LOG_MODULE_REGISTER(Marmoset_FMW, LOG_LEVEL_INF);

#define SD_CARD_THREAD_PRIORITY 3
#define FAKEDATA_THREAD_PRIORITY 0
#define INTAN_THREAD_PRIORITY 0

fifo_buffer_t fifo_buffer;

// Define and initialize the device status with default values
DeviceStatus device_status = {
	.battery_level = 100,
	.temperature = 25,
	.recording_status = true,
	.configuration = "v0.0.1"};

int main(void)
{
	int err;

	// Wait for 100ms to allow the system to stabilize
	k_sleep(K_MSEC(100));
	LOG_INF("Marmoset FMW V0 \n");

	// Initialize Bluetooth ============================================================
	err = ble_init();
	if (err)
	{
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		sys_reboot(SYS_REBOOT_COLD);
		return -1;
	}
	LOG_INF("Bluetooth initialized");
	k_sleep(K_MSEC(100));

	// Wait for connection
	LOG_INF("Waiting for Bluetooth connection...");
	err = ble_wait_for_connection(K_SECONDS(30)); // Wait for up to 30 seconds
	if (err)
	{
		LOG_ERR("Failed to establish Bluetooth connection within timeout, rebooting...");
		sys_reboot(SYS_REBOOT_COLD);
		return -1;
	}
	LOG_INF("Bluetooth connection established");
	k_sleep(K_MSEC(100));

	// Initialize SD card ============================================================ (commented out for first in-vivo, NO SD YET)
	// LOG_INF("Initializing SD card...");
	// err = sd_card_init();
	// if (err)
	// {
	// 	LOG_ERR("SD card initialization failed (err %d)", err);
	// 	sys_reboot(SYS_REBOOT_COLD);
	// 	return -1;
	// }
	// LOG_INF("SD card initialized");
	// k_sleep(K_MSEC(100));

	// Initialize FIFO buffer ============================================================
	LOG_INF("Initializing FIFO buffer...");
	err = init_fifo_buffer(&fifo_buffer);
	if (err)
	{
		LOG_ERR("FIFO buffer initialization failed (err %d)", err);
		sys_reboot(SYS_REBOOT_COLD);
		return -1;
	}
	LOG_INF("FIFO buffer initialized successfully");
	k_sleep(K_MSEC(100));

	// Initialize Intan ============================================================
	err = intan_init(&fifo_buffer);
	if (err)
	{
		LOG_ERR("Intan initialization failed (err %d)", err);
		sys_reboot(SYS_REBOOT_COLD);
		return -1;
	}
	LOG_INF("Intan initialized successfully");
	k_sleep(K_MSEC(100));

	LOG_INF("=======!!! All systems initialized !!!======= \n");
	k_sleep(K_MSEC(100));

	// Create threads dynamically ============================================================

	// (BLE module starts BLE threads)

	k_thread_create(&sd_card_thread_data, sd_card_stack,
					SD_CARD_THREAD_STACK_SIZE,
					sd_card_writer_thread, &fifo_buffer, NULL, NULL,
					SD_CARD_THREAD_PRIORITY, 0, K_MSEC(2000));
	LOG_INF("SD card writer thread created");

	// k_thread_create(&fakedata_thread_data, fakedata_stack,
	// 				FAKEDATA_THREAD_STACK_SIZE,
	// 				fakedata_thread, &fifo_buffer, NULL, NULL,
	// 				FAKEDATA_THREAD_PRIORITY, 0, K_MSEC(10000));
	// LOG_INF("Fakedata thread created");

	k_thread_create(&intan_thread_data, intan_stack,
					INTAN_THREAD_STACK_SIZE,
					intan_thread, &fifo_buffer, NULL, NULL,
					INTAN_THREAD_PRIORITY, 0, K_MSEC(2500));
	LOG_INF("Intan thread created");

	LOG_INF("=======!!! All threads created successfully !!!======= \n");

	return 0;
}
