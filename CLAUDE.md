# CLAUDE.md - Marmoset Neural Recording Firmware

This document provides essential context for AI assistants working on this codebase.

## Project Overview

**MARM (Marmoset) FMW v0** is embedded firmware for a neural data acquisition device targeting marmoset/small primate research. It runs on the Nordic nRF52840 microcontroller using Zephyr RTOS.

**Key Capabilities:**
- Real-time 16-channel neural data acquisition from Intan RHD2232 amplifier
- Bluetooth Low Energy streaming with low-latency connection parameters
- SD card logging for persistent storage
- Dual-consumer FIFO buffer with priority-based overflow handling

**License:** MIT (Copyright 2024 Lampadare)

## Repository Structure

```
MARM_fmw_v0/
├── src/                    # C source files (7 modules)
├── inc/                    # Header files (8 headers)
├── boards/                 # Device tree overlays for nRF52840DK
├── dts/bindings/           # Custom device tree bindings (Intan sensor)
├── scripts/                # Python data processing utilities
├── Mech-Electro/           # Hardware design files (CAD/PCB)
├── Presentations-Reports/  # Project documentation PDFs
├── _oldsrc/                # Deprecated legacy code
├── build/                  # Build artifacts (CMake/Ninja output)
├── CMakeLists.txt          # Build configuration
├── prj.conf                # Zephyr project configuration
└── notes.md                # Development TODO list
```

## Build System

**Platform:** Nordic nRF52840 (ARM Cortex-M4 @ 64 MHz)
**RTOS:** Zephyr RTOS via Nordic Connect SDK (NCS)
**Build:** CMake + Ninja

### Building
```bash
# Standard Zephyr build
west build -b nrf52840dk_nrf52840

# Flash to device
west flash
```

### Key Configuration (prj.conf)
- BLE 2M PHY with low-latency mode (`CONFIG_BT_CTLR_SDC_LLPM=y`)
- Extended MTU: 247 bytes L2CAP, 251 bytes data length
- Stack sizes: 4KB system workqueue, 8KB main thread
- SPI drivers for Intan (SPIM1) and SD card (SPI3/SDMMC)

## Architecture

### Core Data Flow
```
Intan RHD2232 → SPI → FIFO Buffer → BLE notifications → Client
      (ADC)            (300 samples)   ↓
                                    SD Card (FAT filesystem)
```

### Key Data Structure
```c
// 36 bytes per sample (neural_data.h)
typedef struct {
    uint16_t channel_data[16];  // 16 channels × 2 bytes
    uint32_t timestamp;          // 4 bytes
} NeuralData;
```

### Dual-Consumer FIFO
The FIFO supports two independent readers (BLE and SD) with separate read indices:
- **BLE priority (0):** Low-latency streaming, 10-sample threshold
- **SD priority (1):** Bulk writes, 200-sample threshold
- On overflow, discards data from the lower-priority consumer

### Threading Model
| Thread | Priority | Purpose |
|--------|----------|---------|
| intan_thread | 0 | ADC sampling from Intan |
| sd_card_writer_thread | 3 | Writes FIFO data to SD |
| neural_data_notify_thread | 4 | BLE notifications |
| status_notify_thread | 8 | Device status updates |

## Source Files Reference

| File | Purpose |
|------|---------|
| `src/main.c` | Entry point, initialization sequence, thread creation |
| `src/ble_module.c` | BLE stack init, advertising, connection management, PHY negotiation |
| `src/neuralbs.c` | Custom GATT service (Neural Bluetooth Service) with 2 characteristics |
| `src/fifo_buffer.c` | Circular buffer with dual-reader support and priority overflow |
| `src/intan.c` | SPI driver for Intan RHD2232 neural amplifier |
| `src/sd_card.c` | FAT filesystem operations, write-from-FIFO thread |
| `src/fakedata_module.c` | Test data generator (currently disabled) |

## Hardware Configuration

### Device Tree Overlay (`boards/nrf52840dk_nrf52840.overlay`)
- **SPI1:** Intan RHD2232 @ 20 MHz, CS on GPIO0 pin 29
- **SPI3:** SD card via SDMMC @ 20 MHz, CS on GPIO1 pin 12
- QSPI and PWM0 disabled to free resources

### Intan RHD2232 Specifications
- 16-channel neural amplifier
- ADC scale: 0.195 µV/bit (RHD2000 standard)
- Bandwidth: 1 Hz - 300 Hz (configurable)
- Sampling: 250 Hz in current configuration

### BLE Service UUIDs
- **Service:** `ac9a900b-d5c2-4eea-a18b-c30efc00d25e`
- **Neural Data Characteristic:** `bcd5243f-0607-4899-afda-999999999999`
- **Device Status Characteristic:** `d3171a00-57e9-476d-a6db-111111111111`

## Code Conventions

### Naming
- **Types:** PascalCase (`NeuralData`, `DeviceStatus`, `fifo_buffer_t`)
- **Functions:** snake_case (`init_fifo_buffer()`, `ble_wait_for_connection()`)
- **Macros:** UPPERCASE (`FIFO_BUFFER_SIZE`, `MAX_CHANNELS`)

### File Headers
All source files start with:
```c
// Marmoset FMW V0
// module_name.c
```

### Thread Definitions
```c
// Thread data structure
k_thread module_thread_data;
// Stack array
K_THREAD_STACK_DEFINE(module_stack, STACK_SIZE);
// Thread function signature
void module_thread(void *arg1, void *arg2, void *arg3);
```

### Error Handling
- Return `int` with standard errno values (-EINVAL, -ENODEV, -EPERM)
- Use Zephyr logging: `LOG_INF()`, `LOG_ERR()`, `LOG_WRN()`
- Register module loggers: `LOG_MODULE_REGISTER(module_name, LOG_LEVEL_INF)`

### Include Guards
```c
#ifndef MODULE_NAME_H
#define MODULE_NAME_H
// ...
#endif /* MODULE_NAME_H */
```

## Python Scripts (scripts/)

Data processing utilities for recorded neural data:

| Script | Purpose |
|--------|---------|
| `binarydecoder.py` | Convert binary `data_*.bin` files to CSV |
| `process_neural_data.py` | Advanced DSP and multi-threaded processing |
| `plot_neural_data.py` | Time-series visualization |
| `large_plotter.py` | Large dataset visualization |
| `test_ble_log_decoding.py` | BLE packet decoding tests |
| `poweranalyser/` | Power consumption analysis tools |

### Binary Data Format
- 36 bytes per sample: 16 × uint16_t (channels) + uint32_t (timestamp)
- Little-endian packed
- ADC conversion: raw_value × 0.195 = µV

## Non-Code Resources

### Mechanical/Electrical Design (Mech-Electro/)
- **MARM-CAD/**: 3D CAD models (STEP format) V1-V4 iterations
- **PCB/**: KiCad PCB design files (zipped)
  - `Marmoset-PCB-DuBo.zip` - Bottom layer
  - `Marmoset-PCB-DuTop.zip` - Top layer

### Documentation (Presentations-Reports/)
- `MARM-VIVA.pdf` - Presentation slides
- `MartinLombard_Final_Report_v3.pdf` - Final project report
- `final-MarmosetProject-PlanningReport-MartinLombard.pdf` - Planning documentation

### Test Data (scripts/)
- `session_files/` - Recorded neural data sessions
- `throughputdata/` - BLE throughput test logs (b1-b15.txt, fb1-fb9.txt)

## Current Development Status

### Completed
- SD card driver and FAT filesystem integration
- Dual-reader FIFO buffer with priority overflow
- Intan RHD2232 SPI driver and ADC configuration
- BLE GATT service with neural + status characteristics
- Low-latency BLE connection parameter negotiation
- Multi-threaded data pipeline

### In Progress (from notes.md)
- Semaphore implementation to prevent FIFO overflow
- Neural network for gap-filling missing packets

### Known Limitations
- SD card initialization is currently commented out in main.c
- Fakedata thread is disabled
- First in-vivo testing focused on BLE-only operation

## Common Tasks

### Adding a New BLE Characteristic
1. Define UUID in `inc/neuralbs.h`
2. Add characteristic to GATT service in `src/neuralbs.c`
3. Implement notification function following existing patterns

### Modifying FIFO Behavior
- Buffer size: `FIFO_BUFFER_SIZE` in `inc/fifo_buffer.h`
- Priority: `FIFO_PRIORITY_BLE` / `FIFO_PRIORITY_SD`
- Thresholds: Adjust in fifo_buffer.c read functions

### Changing BLE Parameters
- Connection intervals: `CONFIG_BT_PERIPHERAL_PREF_*` in `prj.conf`
- Data length: `CONFIG_BT_CTLR_DATA_LENGTH_MAX` in `prj.conf`
- Device name: `CONFIG_BT_DEVICE_NAME` in `prj.conf`

### Processing Recorded Data
```bash
# Convert binary to CSV
python scripts/binarydecoder.py <input_folder> <output.csv>
```

## Important Notes for AI Assistants

1. **This is embedded firmware** - changes affect real hardware. Test thoroughly.
2. **Thread safety matters** - FIFO operations are mutex-protected; maintain this pattern.
3. **Memory is limited** - nRF52840 has 256KB RAM. Monitor stack usage.
4. **BLE payload limit** - 244 bytes max = 6 NeuralData samples per notification.
5. **SD card is disabled** - Currently commented out for in-vivo testing.
6. **Intan commands** - SPI protocol specific to RHD2000 series; refer to Intan datasheet.
7. **Zephyr conventions** - Follow Zephyr RTOS patterns for threads, drivers, and logging.
