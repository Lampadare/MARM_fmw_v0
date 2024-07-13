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

void init_fifo_buffer(fifo_buffer_t *fifo_buffer)
{
    fifo_buffer->head = 0;
    fifo_buffer->tail = 0;
    fifo_buffer->size = 0;
    k_mutex_init(&fifo_buffer->mutex);
    k_sem_init(&fifo_buffer->data_available, 0, 1); // Initialize semaphore
}

size_t read_from_fifo_buffer(fifo_buffer_t *fifo_buffer, NeuralData *data, size_t max_size)
{
    size_t structs_read = 0;
    bool was_above_threshold = false;

    int ret = k_mutex_lock(&fifo_buffer->mutex, K_NO_WAIT);
    if (ret != 0)
    {
        LOG_WRN("Failed to acquire mutex, error: %d", ret);
        k_mutex_unlock(&fifo_buffer->mutex);
        return 0;
    }

    // Check if the buffer was above 50% before reading
    was_above_threshold = fifo_buffer->size >= (FIFO_BUFFER_SIZE / 2);

    while (structs_read < max_size && fifo_buffer->size > 0)
    {
        *data = fifo_buffer->buffer[fifo_buffer->head]; // get the pointer to the head in the buffer
        fifo_buffer->head = (fifo_buffer->head + 1) % FIFO_BUFFER_SIZE;
        fifo_buffer->size--;
        data++;
        structs_read++;
    }

    // In read_from_fifo_buffer and write_to_fifo_buffer:
    int fill_percentage = (int)((fifo_buffer->size * 100) / FIFO_BUFFER_SIZE);
    if (log_counter++ % 100 == 0 || (fill_percentage - last_fill_percentage > 5) || (last_fill_percentage - fill_percentage > 5))
    {
        LOG_INF("FIFO Buffer fill: %d%% (read %zu structs)", fill_percentage, structs_read);
        last_fill_percentage = fill_percentage;
    }

    // Sem taken in SD card writer thread
    // if (was_above_threshold && fifo_buffer->size < (FIFO_BUFFER_SIZE / 2))
    // {
    //     // Reset the semaphore when falling below 50% threshold
    //     k_sem_take(&fifo_buffer->data_available, K_NO_WAIT);
    //     LOG_INF("Buffer fell below 50 percent capacity, reset data availability signal");
    // }

    k_mutex_unlock(&fifo_buffer->mutex);

    return structs_read;
}

size_t write_to_fifo_buffer(fifo_buffer_t *fifo_buffer, const NeuralData *data, size_t size)
{
    size_t structs_written = 0;
    bool was_below_threshold = false;

    int ret = k_mutex_lock(&fifo_buffer->mutex, K_NO_WAIT);
    if (ret != 0)
    {
        LOG_WRN("Failed to acquire mutex, error: %d", ret);
        k_mutex_unlock(&fifo_buffer->mutex);
        return 0;
    }

    // Check if the buffer was below 50% before writing
    was_below_threshold = fifo_buffer->size < (FIFO_BUFFER_SIZE / 2);

    while (structs_written < size && fifo_buffer->size < FIFO_BUFFER_SIZE)
    {
        fifo_buffer->buffer[fifo_buffer->tail] = *data;
        fifo_buffer->tail = (fifo_buffer->tail + 1) % FIFO_BUFFER_SIZE;
        fifo_buffer->size++;
        data++;
        structs_written++;
    }

    // In read_from_fifo_buffer and write_to_fifo_buffer:
    int fill_percentage = (int)((fifo_buffer->size * 100) / FIFO_BUFFER_SIZE);
    if (log_counter++ % 100 == 0 || (fill_percentage - last_fill_percentage > 5) || (last_fill_percentage - fill_percentage > 5))
    {
        LOG_INF("FIFO Buffer fill: %d%% (wrote %zu structs)", fill_percentage, structs_written);
        last_fill_percentage = fill_percentage;
    }

    // if (structs_written < size)
    // {
    //     LOG_WRN("FIFO Buffer full, dropped %zu structs", size - structs_written);
    // }

    // Check if the buffer has reached or exceeded 50% capacity
    if (fifo_buffer->size >= (FIFO_BUFFER_SIZE / 2))
    {
        // Signal that data is available only when crossing the 50% threshold
        k_sem_give(&fifo_buffer->data_available);
        LOG_INF("Buffer reached 50 percent capacity, signaled data availability");
    }

    k_mutex_unlock(&fifo_buffer->mutex);

    return structs_written;
}

int get_fifo_fill_percentage(fifo_buffer_t *fifo_buffer)
{
    return (fifo_buffer->size / FIFO_BUFFER_SIZE) * 100;
}