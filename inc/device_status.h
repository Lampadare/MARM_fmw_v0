// device_status.h
#ifndef DEVICE_STATUS_H
#define DEVICE_STATUS_H

#include <stdint.h>
#define CONFIG_VERSION_LENGTH 8

typedef struct
{
    uint8_t battery_level;
    int8_t temperature;
    bool recording_status;
    char configuration[CONFIG_VERSION_LENGTH + 1];
} DeviceStatus;

// Declare the global variable
extern DeviceStatus device_status;

#endif // DEVICE_STATUS_H
