// fifo_buffer.h

#ifndef FIFO_BUFFER_H
#define FIFO_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include "../inc/neural_data.h"

// Half a second of data to be buffered represents roughly 200 neural data items
#define FIFO_BUFFER_SIZE 300
#define MAX_FIFO_DATA_SIZE 244

typedef struct
{
    NeuralData buffer[FIFO_BUFFER_SIZE];
    size_t head;
    size_t tail;
    size_t size;
    struct k_mutex mutex;
    struct k_sem data_available; // Semaphore to signal data availability
} fifo_buffer_t;

int init_fifo_buffer(fifo_buffer_t *fifo_buffer);
size_t read_from_fifo_buffer(fifo_buffer_t *fifo_buffer, NeuralData *data, size_t max_size);
size_t write_to_fifo_buffer(fifo_buffer_t *fifo_buffer, const NeuralData *data, size_t size);
int get_fifo_fill_percentage(fifo_buffer_t *fifo_buffer);

#endif /* FIFO_BUFFER_H */