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

    k_mutex_lock(&fifo_buffer->mutex, K_FOREVER);

    // Check if the buffer was above 50% before reading
    was_above_threshold = fifo_buffer->size >= (FIFO_BUFFER_SIZE / 2);

    while (structs_read < max_size && fifo_buffer->size > 0)
    {
        *data = fifo_buffer->buffer[fifo_buffer->head];
        fifo_buffer->head = (fifo_buffer->head + 1) % FIFO_BUFFER_SIZE;
        fifo_buffer->size--;
        data++;
        structs_read++;
    }

    float fill_percentage = (float)fifo_buffer->size / FIFO_BUFFER_SIZE * 100;
    LOG_INF("FIFO Buffer fill: %.2f%% (read %zu structs)", fill_percentage, structs_read);

    // Check if the buffer has fallen below 50% capacity
    if (was_above_threshold && fifo_buffer->size < (FIFO_BUFFER_SIZE / 2))
    {
        // Reset the semaphore when falling below 50% threshold
        k_sem_reset(&fifo_buffer->data_available);
        LOG_INF("Buffer fell below 50 percent capacity, reset data availability signal");
    }

    k_mutex_unlock(&fifo_buffer->mutex);

    return structs_read;
}

size_t write_to_fifo_buffer(fifo_buffer_t *fifo_buffer, const NeuralData *data, size_t size)
{
    size_t structs_written = 0;
    bool was_below_threshold = false;

    k_mutex_lock(&fifo_buffer->mutex, K_FOREVER);

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

    float fill_percentage = (float)fifo_buffer->size / FIFO_BUFFER_SIZE * 100;
    LOG_INF("FIFO Buffer fill: %.2f%% (wrote %zu structs)", fill_percentage, structs_written);

    if (structs_written < size)
    {
        LOG_WRN("FIFO Buffer full, dropped %zu structs", size - structs_written);
    }

    // Check if the buffer has reached or exceeded 50% capacity
    if (was_below_threshold && fifo_buffer->size >= (FIFO_BUFFER_SIZE / 2))
    {
        // Signal that data is available only when crossing the 50% threshold
        k_sem_give(&fifo_buffer->data_available);
        LOG_INF("Buffer reached 50 percent capacity, signaled data availability");
    }

    k_mutex_unlock(&fifo_buffer->mutex);

    return structs_written;
}