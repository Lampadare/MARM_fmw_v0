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

K_THREAD_STACK_DEFINE(fakedata_stack, FAKEDATA_THREAD_STACK_SIZE); // Define the stack for the fakedata thread
struct k_thread fakedata_thread_data;                              // Declare the thread data structure for the fakedata thread

/* Thread function to generate and send fake data, takes in a pointer to the fifo buffer */
void fakedata_thread(void *arg1, void *arg2, void *arg3)
{
    fifo_buffer_t *fifo_buffer = (fifo_buffer_t *)arg1; // Cast the argument to the appropriate fifo_buffer_t type
    NeuralData data;                                    // Declare the data structure to hold neural data
    uint16_t counter[MAX_CHANNELS] = {0};               // Initialize counters for each channel
    int64_t start_time = k_uptime_get();                // Record the start time for timestamp calculations

    /* Infinite loop to continuously generate data */
    while (1)
    {
        data.timestamp = (uint32_t)(k_uptime_get() - start_time); // Update the timestamp for the data

        // Generate data for each channel
        for (int i = 0; i < MAX_CHANNELS; i++)
        {
            for (int j = 0; j < MAX_BYTES_PER_CHANNEL; j++)
            {
                data.channels[i].data[j] = counter[i] % 256; // Simple modulo operation for fake data
            }
            counter[i]++; // Increment the counter for the next data point
        }

        // Write the NeuralData struct to the FIFO buffer
        size_t structs_written = write_to_fifo_buffer(fifo_buffer, &data, 1);
        // Check if the struct was successfully written
        if (structs_written != 1)
        {
            LOG_ERR("Failed to write neural data to FIFO buffer.");
        }

        // Update the global latest_neural_data variable
        latest_neural_data = data;

        LOG_INF("Faked data written to fifo buffer: %d", latest_neural_data.timestamp);

        // Sleep to maintain the specified sample rate
        k_sleep(K_MSEC(1000 / SAMPLE_RATE_HZ));
    }
}