// Marmoset FMW V0
// fifo_buffer.h

#ifndef FIFO_BUFFER_H
#define FIFO_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include "../inc/neural_data.h"

/* FIFO Buffer size definitions */
#define FIFO_BUFFER_SIZE 300
#define MAX_FIFO_DATA_SIZE 244

/* Priority definitions */
#define FIFO_PRIORITY_BLE 0
#define FIFO_PRIORITY_SD 1

typedef struct
{
    NeuralData buffer[FIFO_BUFFER_SIZE];
    size_t write_index;     // Next position to write new data.
    size_t read_index_ble;  // Read index for BLE consumer.
    size_t read_index_sd;   // Read index for SD consumer.
    size_t ble_buffer_size; // Current number of unread elements for BLE.
    size_t sd_buffer_size;  // Current number of unread elements for SD.
    uint8_t priority;       // Overflow priority: FIFO_PRIORITY_BLE or FIFO_PRIORITY_SD.
    struct k_mutex mutex;
    struct k_sem ble_data_available; // Semaphore to signal BLE data availability.
    struct k_sem sd_data_available;  // Semaphore to signal SD data availability.
} fifo_buffer_t;

/* Function prototypes */
int init_fifo_buffer(fifo_buffer_t *fifo_buffer);
size_t read_from_fifo_buffer_ble(fifo_buffer_t *fifo_buffer, NeuralData *data, size_t max_size);
size_t read_from_fifo_buffer_sd(fifo_buffer_t *fifo_buffer, NeuralData *data, size_t max_size);
size_t write_to_fifo_buffer(fifo_buffer_t *fifo_buffer, const NeuralData *data, size_t size);
int get_fifo_fill_percentage(fifo_buffer_t *fifo_buffer);

#endif /* FIFO_BUFFER_H */