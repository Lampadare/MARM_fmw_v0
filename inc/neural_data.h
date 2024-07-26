// neural_data.h
#ifndef NEURAL_DATA_H
#define NEURAL_DATA_H

#include <stdint.h>
#include <zephyr/kernel.h>

#define MAX_CHANNELS 16

// Structure to hold ONE SAMPLE of neural data (totals 288 bits = 36 bytes)
typedef struct
{
    uint16_t channel_data[MAX_CHANNELS];
    uint32_t timestamp;
} NeuralData;

// Declare the global variable
typedef struct
{
    NeuralData data;
    bool sent;
} LatestNeuralData;

extern LatestNeuralData latest_neural_data;

#endif // NEURAL_DATA_H
