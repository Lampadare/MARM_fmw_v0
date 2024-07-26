// fakedata_module.c

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/logging/log.h>
#include "../inc/fakedata_module.h"
#include "../inc/neural_data.h"
#include "../inc/fifo_buffer.h"

#define SAMPLE_RATE_HZ 130

// Registering the module with the logging system
LOG_MODULE_REGISTER(fakedata, LOG_LEVEL_INF);

K_THREAD_STACK_DEFINE(fakedata_stack, FAKEDATA_THREAD_STACK_SIZE);
struct k_thread fakedata_thread_data;

void fakedata_thread(void *arg1, void *arg2, void *arg3)
{
    fifo_buffer_t *fifo_buffer = (fifo_buffer_t *)arg1;
    NeuralData data;
    uint16_t counter = 0;
    int64_t start_time = k_uptime_get();
    static int log_counter = 0;

    while (1)
    {
        if (get_fifo_fill_percentage(fifo_buffer) > 90)
        {
            if (log_counter++ % 50 == 0)
            {
                LOG_WRN("FIFO buffer nearly full, skipping data generation");
            }
            k_sleep(K_MSEC(50));
            continue;
        }

        data.timestamp = (uint32_t)(k_uptime_get() - start_time);

        // Generate data for all channels
        for (int i = 0; i < MAX_CHANNELS; i++)
        {
            data.channel_data[i] = counter;
        }

        // Write the NeuralData struct to the FIFO buffer
        size_t structs_written = write_to_fifo_buffer(fifo_buffer, &data, 1);
        if (structs_written != 1)
        {
            LOG_ERR("Failed to write neural data to FIFO buffer.");
        }

        // Update the global latest_neural_data variable
        latest_neural_data.data = data;
        latest_neural_data.sent = false;

        if (log_counter++ % 100 == 0)
        {
            LOG_INF("Faked data written to fifo buffer: timestamp %d, value %d",
                    data.timestamp, data.channel_data[0]);
        }

        counter = (counter + 1) % 60000; // Increment counter from 0 to 60000

        k_sleep(K_MSEC(1000 / SAMPLE_RATE_HZ));
    }
}