// Marmoset FMW V0
// fifo_buffer.c

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "../inc/fifo_buffer.h"
#include "../inc/neural_data.h"

LOG_MODULE_REGISTER(fifo_buffer, LOG_LEVEL_INF);
// Only log every 100th operation or when fill percentage changes significantly
static int log_counter = 0;
static float last_fill_percentage = 0;

// Data availability threshold for BLE and SD consumers.
#define BLE_BUFFER_FILL_FLAG_STRUCTS 10
#define SD_BUFFER_FILL_FLAG_STRUCTS 200

/**
 * @brief Initialize the FIFO buffer.
 * Sets write index, both read indices, and resets the buffer sizes.
 * Also sets the priority (here, default is set to BLE has priority).
 */
int init_fifo_buffer(fifo_buffer_t *fifo_buffer)
{
    if (fifo_buffer == NULL)
    {
        return -EINVAL; // Invalid argument
    }

    fifo_buffer->write_index = 0;
    fifo_buffer->read_index_ble = 0;
    fifo_buffer->read_index_sd = 0;
    fifo_buffer->ble_buffer_size = 0;
    fifo_buffer->sd_buffer_size = 0;
    fifo_buffer->priority = FIFO_PRIORITY_BLE; // Set BLE as the priority consumer

    int ret = k_mutex_init(&fifo_buffer->mutex);
    if (ret != 0)
    {
        return ret; // Return the error code from mutex initialization
    }

    // Initialize semaphore for BLE data availability
    ret = k_sem_init(&fifo_buffer->ble_data_available, 0, 1);
    if (ret != 0)
    {
        return ret;
    }

    // Initialize semaphore for SD data availability
    ret = k_sem_init(&fifo_buffer->sd_data_available, 0, 1);
    if (ret != 0)
    {
        return ret;
    }

    return 0; // Success
}

/**
 * @brief Write data to the FIFO buffer.
 *
 * @param fifo_buffer Pointer to the FIFO buffer.
 * @param data Pointer to the data to write.
 * @param size Number of elements to write.
 */
size_t write_to_fifo_buffer(fifo_buffer_t *fifo_buffer, const NeuralData *data, size_t size)
{
    size_t structs_written = 0;
    int ret = k_mutex_lock(&fifo_buffer->mutex, K_FOREVER);
    if (ret != 0)
    {
        LOG_WRN("Failed to acquire mutex, error: %d", ret);
        return 0;
    }

    while (structs_written < size)
    {
        size_t next_write_index = (fifo_buffer->write_index + 1) % FIFO_BUFFER_SIZE;

        // Check for buffer full condition:
        // If next write index equals either consumer's read index, the buffer is "full".
        if (next_write_index == fifo_buffer->read_index_ble ||
            next_write_index == fifo_buffer->read_index_sd)
        {
            // Decide which consumer to advance based on priority.
            // If BLE has priority, discard from SD by advancing the SD read index.
            // Otherwise, advance the BLE read index.
            if (fifo_buffer->priority == FIFO_PRIORITY_BLE)
            {
                fifo_buffer->read_index_sd = (fifo_buffer->read_index_sd + 1) % FIFO_BUFFER_SIZE;
            }
            else
            {
                fifo_buffer->read_index_ble = (fifo_buffer->read_index_ble + 1) % FIFO_BUFFER_SIZE;
            }
            // After discarding, update the buffer sizes
            fifo_buffer->sd_buffer_size = (fifo_buffer->write_index + FIFO_BUFFER_SIZE - fifo_buffer->read_index_sd) % FIFO_BUFFER_SIZE;
            fifo_buffer->ble_buffer_size = (fifo_buffer->write_index + FIFO_BUFFER_SIZE - fifo_buffer->read_index_ble) % FIFO_BUFFER_SIZE;
            // Continue without writing to free up a slot
            continue;
        }

        // Write the sample to the buffer
        fifo_buffer->buffer[fifo_buffer->write_index] = *data;
        fifo_buffer->write_index = next_write_index;
        structs_written++;

        // Update buffer sizes for each consumer
        fifo_buffer->ble_buffer_size = (fifo_buffer->write_index + FIFO_BUFFER_SIZE - fifo_buffer->read_index_ble) % FIFO_BUFFER_SIZE;
        fifo_buffer->sd_buffer_size = (fifo_buffer->write_index + FIFO_BUFFER_SIZE - fifo_buffer->read_index_sd) % FIFO_BUFFER_SIZE;

        data++;
    }

    // Log buffer fill status
    int fill_percentage = (int)(((fifo_buffer->ble_buffer_size > fifo_buffer->sd_buffer_size ? fifo_buffer->ble_buffer_size : fifo_buffer->sd_buffer_size) * 100) / FIFO_BUFFER_SIZE);
    if (log_counter++ % 150 == 0 ||
        (abs(fill_percentage - (int)last_fill_percentage) > 10))
    {
        LOG_INF("FIFO Buffer fill: %d%% (wrote %zu structs)", fill_percentage, structs_written);
        last_fill_percentage = fill_percentage;
    }

    // Signal data availability if threshold is reached.
    // For example, if BLE unread count exceeds a given threshold.
    if (fifo_buffer->ble_buffer_size >= BLE_BUFFER_FILL_FLAG_STRUCTS)
    {
        k_sem_give(&fifo_buffer->ble_data_available);
        LOG_INF("FIFO: BLE buffer reached threshold (%zu structs)", fifo_buffer->ble_buffer_size);
    }
    if (fifo_buffer->sd_buffer_size >= SD_BUFFER_FILL_FLAG_STRUCTS)
    {
        k_sem_give(&fifo_buffer->sd_data_available);
        LOG_INF("FIFO: SD buffer reached threshold (%zu structs)", fifo_buffer->sd_buffer_size);
    }

    k_mutex_unlock(&fifo_buffer->mutex);
    return structs_written;
}

/**
 * @brief Read data for the BLE consumer.
 *
 * Reads up to max_size NeuralData samples from the FIFO using the BLE read index.
 */
size_t read_from_fifo_buffer_ble(fifo_buffer_t *fifo_buffer, NeuralData *data, size_t max_size)
{
    // Initialize the number of structs read to 0.
    size_t structs_read = 0;
    // Lock the mutex to prevent race conditions.
    k_mutex_lock(&fifo_buffer->mutex, K_FOREVER);
    // Read the data from the buffer until the max size is reached or the buffer is empty.
    while (structs_read < max_size && fifo_buffer->read_index_ble != fifo_buffer->write_index)
    {
        *data = fifo_buffer->buffer[fifo_buffer->read_index_ble];
        fifo_buffer->read_index_ble = (fifo_buffer->read_index_ble + 1) % FIFO_BUFFER_SIZE;
        structs_read++;
        data++;
    }
    // Update the buffer size for the BLE consumer.
    fifo_buffer->ble_buffer_size = (fifo_buffer->write_index + FIFO_BUFFER_SIZE - fifo_buffer->read_index_ble) % FIFO_BUFFER_SIZE;
    // Unlock the mutex.
    k_mutex_unlock(&fifo_buffer->mutex);
    // Return the number of structs read.
    return structs_read;
}

/**
 * @brief Read data for the SD consumer.
 *
 * Reads up to max_size NeuralData samples from the FIFO using the SD read index.
 */
size_t read_from_fifo_buffer_sd(fifo_buffer_t *fifo_buffer, NeuralData *data, size_t max_size)
{
    // Initialize the number of structs read to 0.
    size_t structs_read = 0;
    // Lock the mutex to prevent race conditions.
    k_mutex_lock(&fifo_buffer->mutex, K_FOREVER);
    // Read the data from the buffer until the max size is reached or the buffer is empty.
    while (structs_read < max_size && fifo_buffer->read_index_sd != fifo_buffer->write_index)
    {
        *data = fifo_buffer->buffer[fifo_buffer->read_index_sd];
        fifo_buffer->read_index_sd = (fifo_buffer->read_index_sd + 1) % FIFO_BUFFER_SIZE;
        structs_read++;
        data++;
    }
    // Update the buffer size for the SD consumer.
    fifo_buffer->sd_buffer_size = (fifo_buffer->write_index + FIFO_BUFFER_SIZE - fifo_buffer->read_index_sd) % FIFO_BUFFER_SIZE;
    // Unlock the mutex.
    k_mutex_unlock(&fifo_buffer->mutex);
    // Return the number of structs read.
    return structs_read;
}

/**
 * @brief Get the fill percentage of the FIFO.
 *
 * This calculates the fill based on the slower (i.e. least advanced) consumer.
 */
int get_fifo_fill_percentage(fifo_buffer_t *fifo_buffer)
{
    size_t slowest_index = fifo_buffer->read_index_ble <= fifo_buffer->read_index_sd ? fifo_buffer->read_index_ble : fifo_buffer->read_index_sd;
    size_t total_elements = (fifo_buffer->write_index + FIFO_BUFFER_SIZE - slowest_index) % FIFO_BUFFER_SIZE;
    return (total_elements * 100) / FIFO_BUFFER_SIZE;
}
