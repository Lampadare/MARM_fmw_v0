// fakedata_module.c

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/logging/log.h>
#include "../inc/fakedata_module.h"

#define SAMPLE_RATE_HZ 1
#define DATA_RATE_KB_PER_SEC 128

// Registering the module with the logging system
LOG_MODULE_REGISTER(fakedata, LOG_LEVEL_INF);

K_THREAD_STACK_DEFINE(fakedata_stack, FAKEDATA_THREAD_STACK_SIZE); // Define the stack for the fakedata thread
struct k_thread fakedata_thread_data;                              // Declare the thread data structure for the fakedata thread

/* Thread function to generate and send fake data, takes in a pointer to the fifo buffer */
void fakedata_thread(void *arg1, void *arg2, void *arg3)
{
    fifo_buffer_t *fifo_buffer = (fifo_buffer_t *)arg1; // Cast the argument to the appropriate fifo_buffer_t type
    neural_data_t data;                                 // Declare the data structure to hold neural data
    uint16_t counter[NUM_CHANNELS] = {0};               // Initialize counters for each channel
    int64_t start_time = k_uptime_get();                // Record the start time for timestamp calculations

    /* Infinite loop to continuously generate data */
    while (1)
    {
        data.timestamp = k_uptime_get() - start_time; // Update the timestamp for the data

        // Generate data for each channel
        for (int i = 0; i < NUM_CHANNELS; i++)
        {
            data.data[i] = counter[i] % 1000; // Simple modulo operation for fake data
            counter[i]++;                     // Increment the counter for the next data point
        }

        size_t data_size = sizeof(neural_data_t); // Calculate the size of the data structure
        char str_data[data_size * 2 + 1];         // Buffer to hold the hexadecimal string representation of data
        char *ptr = str_data;                     // Pointer to traverse the string buffer

        // Convert binary data to a hexadecimal string
        for (size_t i = 0; i < data_size; i++)
        {
            ptr += sprintf(ptr, "%02X", ((char *)&data)[i]);
        }
        *ptr = '\0'; // Null-terminate the string

        // Write the string data to the FIFO buffer
        size_t bytes_written = write_to_fifo_buffer(fifo_buffer, (uint8_t *)str_data, strlen(str_data));
        // Check if all bytes were successfully written
        if (bytes_written != strlen(str_data))
        {
            LOG_ERR("Failed to write all data to FIFO buffer.");
        }

        // Sleep to maintain the specified sample rate
        k_sleep(K_MSEC(1000 / SAMPLE_RATE_HZ));
    }
}