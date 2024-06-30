#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "../inc/fifo_buffer.h"
#include "../inc/neural_data.h"
#include "../inc/sd_card.h" // Assume this header provides SD card functions

#define SD_WRITE_BUFFER_SIZE 4096 // Adjust based on your SD card's optimal write size

LOG_MODULE_REGISTER(sd_card_writer, LOG_LEVEL_INF);

K_THREAD_STACK_DEFINE(sd_card_writer_stack, SD_CARD_WRITER_STACK_SIZE);
struct k_thread sd_card_writer_thread_data;

static uint8_t sd_write_buffer[SD_WRITE_BUFFER_SIZE];
static size_t sd_write_buffer_index = 0;

void sd_card_writer_thread(void *arg1, void *arg2, void *arg3)
{
    fifo_buffer_t *fifo_buffer = (fifo_buffer_t *)arg1;
    NeuralData data;

    while (1)
    {
        // Read data from the FIFO buffer
        size_t structs_read = read_from_fifo_buffer(fifo_buffer, &data, 1);
        if (structs_read == 1)
        {
            // Serialize the NeuralData structure into the write buffer
            memcpy(&sd_write_buffer[sd_write_buffer_index], &data, sizeof(NeuralData));
            sd_write_buffer_index += sizeof(NeuralData);

            // If the write buffer is full, write it to the SD card
            if (sd_write_buffer_index >= SD_WRITE_BUFFER_SIZE)
            {
                if (sd_card_write(sd_write_buffer, SD_WRITE_BUFFER_SIZE) != 0)
                {
                    LOG_ERR("Failed to write data to SD card.");
                }
                sd_write_buffer_index = 0; // Reset the buffer index
            }
        }
        else
        {
            // If no data is read, sleep for a short period before retrying
            k_sleep(K_MSEC(10));
        }
    }
}

void main(void)
{
    // Initialize the SD card
    if (sd_card_init() != 0)
    {
        LOG_ERR("Failed to initialize SD card.");
        return;
    }

    // Start the SD card writer thread
    k_thread_create(&sd_card_writer_thread_data, sd_card_writer_stack,
                    K_THREAD_STACK_SIZEOF(sd_card_writer_stack),
                    sd_card_writer_thread, &fifo_buffer, NULL, NULL,
                    SD_CARD_WRITER_THREAD_PRIORITY, 0, K_NO_WAIT);
}