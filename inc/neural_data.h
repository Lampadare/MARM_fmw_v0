// neural_data.h
#ifndef NEURAL_DATA_H
#define NEURAL_DATA_H

#include <stdint.h>
#include <zephyr/kernel.h>

#define MAX_CHANNELS 16
#define MAX_BYTES_PER_CHANNEL 15

// Structure to hold neural data for one channel
typedef struct
{
    uint8_t data[MAX_BYTES_PER_CHANNEL];
} NeuralChannelData;

// Structure to hold neural data for all channels
typedef struct
{
    NeuralChannelData channels[MAX_CHANNELS];
    uint32_t timestamp; // 32-bit timestamp in milliseconds
} NeuralData;

// Declare the global variable
extern NeuralData latest_neural_data;

#endif // NEURAL_DATA_H
