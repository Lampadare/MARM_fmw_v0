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

    k_mutex_lock(&fifo_buffer->mutex, K_FOREVER);

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

    k_mutex_unlock(&fifo_buffer->mutex);

    return structs_read;
}

size_t write_to_fifo_buffer(fifo_buffer_t *fifo_buffer, const NeuralData *data, size_t size)
{
    size_t structs_written = 0;

    k_mutex_lock(&fifo_buffer->mutex, K_FOREVER);

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

    k_mutex_unlock(&fifo_buffer->mutex);

    // Signal that data is available
    k_sem_give(&fifo_buffer->data_available);

    return structs_written;
}