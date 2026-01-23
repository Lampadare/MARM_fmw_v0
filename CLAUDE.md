# CLAUDE.md - Marmoset Neural Recording Firmware

This document provides essential context for AI assistants working on this codebase.

## Project Overview

**MARM (Marmoset) FMW v0** is embedded firmware for a high-performance neural data acquisition device designed for closed-loop auditory stimulation (CLAS) experiments in free-moving marmoset monkeys. This is an MSc project from Imperial College London (Department of Bioengineering) in collaboration with Newcastle University.

**Author:** Martin Lombard
**Supervisors:** Prof. Tim Constandinou, Dr. Ian Williams
**License:** MIT (Copyright 2024 Lampadare)

**Key Capabilities:**
- Real-time 16-channel neural data acquisition from Intan RHD2132 amplifier
- Bluetooth Low Energy streaming with sub-10ms latency via 2M PHY
- SD card logging for persistent storage (FAT filesystem)
- Dual-consumer FIFO buffer with priority-based overflow handling
- Simultaneous wireless transmission and local storage
- Miniaturized design: 11.05g total weight, 25.2 x 38.1 x 20 mm

**Target Application:** Recording Local Field Potentials (LFPs) from marmoset amygdala/mPFC regions for studying fear network oscillations (theta range, 4-8 Hz).

## Repository Structure

```
MARM_fmw_v0/
├── src/                          # C source files (7 modules)
│   ├── main.c                    # Entry point, initialization, thread spawning
│   ├── ble_module.c              # BLE stack, advertising, PHY/MTU negotiation
│   ├── neuralbs.c                # Custom GATT service (Neural Bluetooth Service)
│   ├── fifo_buffer.c             # Dual-reader circular buffer with priority overflow
│   ├── intan.c                   # SPI driver for Intan RHD2132 neural amplifier
│   ├── sd_card.c                 # FAT filesystem, write-from-FIFO thread
│   └── fakedata_module.c         # Test data generator (disabled)
│
├── inc/                          # Header files (8 headers)
│   ├── ble_module.h              # BLE function declarations
│   ├── fifo_buffer.h             # FIFO buffer types and API
│   ├── neural_data.h             # Core NeuralData struct (36 bytes)
│   ├── neuralbs.h                # GATT service UUIDs and declarations
│   ├── intan.h                   # Intan RHD2132 driver interface
│   ├── sd_card.h                 # SD card operations
│   ├── fakedata_module.h         # Fake data generator
│   └── device_status.h           # DeviceStatus struct for BLE notifications
│
├── boards/                       # Device tree overlays
│   └── nrf52840dk_nrf52840.overlay  # SPI1 (Intan), SPI3 (SD card) config
│
├── dts/bindings/                 # Custom device tree bindings
│   └── intan,rhd2232.yaml        # Intan RHD2232 sensor device binding
│
├── scripts/                      # Python data processing & analysis
│   ├── binarydecoder.py          # Binary → CSV converter
│   ├── process_neural_data.py    # Advanced DSP, multi-threaded processing
│   ├── process_neural_data2.py   # Alternative processing pipeline
│   ├── plot_neural_data.py       # Time-series channel visualization
│   ├── large_plotter.py          # Large dataset visualization
│   ├── plot_attribute_deviation.py  # BLE connection interval analysis
│   ├── test_ble_log_decoding.py  # BLE packet decoding tests
│   ├── poweranalyser/            # Power consumption analysis tools
│   ├── session_files/            # Recorded neural data sessions
│   ├── throughputdata/           # BLE throughput test logs
│   └── throughputdata_out2/      # Processed throughput results
│
├── Mech-Electro/                 # Hardware design files
│   ├── MARM-CAD/                 # 3D CAD models (STEP format)
│   └── PCB/                      # KiCad PCB design files (zipped)
│
├── Presentations-Reports/        # Project documentation (PDFs)
│   ├── MartinLombard_Final_Report_v3.pdf
│   ├── MARM-VIVA.pdf
│   └── final-MarmosetProject-PlanningReport-MartinLombard.pdf
│
├── _oldsrc/                      # Deprecated legacy code
├── build/                        # Build artifacts (CMake/Ninja)
├── .vscode/                      # VS Code workspace settings
│
├── CMakeLists.txt                # Build configuration (7 source files)
├── prj.conf                      # Zephyr project configuration (64 options)
├── notes.md                      # Development TODO list
├── LICENSE                       # MIT License
└── MARM_fmw_v0.code-workspace    # VS Code workspace file
```

## Build System

**Platform:** Nordic nRF52840 (ARM Cortex-M4F @ 64 MHz, 256KB RAM, 1MB Flash)
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
- BLE 2M PHY with Low-Latency Peripheral Mode (`CONFIG_BT_CTLR_SDC_LLPM=y`)
- Connection interval: 1.25ms minimum (`CONFIG_BT_PERIPHERAL_PREF_MIN_INT=1`)
- Extended MTU: 247 bytes L2CAP, 251 bytes data length
- Stack sizes: 4KB system workqueue, 8KB main thread
- SPI drivers: SPIM1 (Intan @ 20MHz), SPI3/SDMMC (SD card @ 20MHz)
- FAT filesystem with long filename support

## Architecture

### Core Data Flow
```
Intan RHD2132 → SPI → FIFO Buffer → BLE notifications → Client
   (16-ch ADC)       (300 samples)      ↓
                                     SD Card (FAT filesystem)
```

### Key Data Structures

**NeuralData** (36 bytes per sample - `inc/neural_data.h`):
```c
typedef struct {
    uint16_t channel_data[16];  // 16 channels × 2 bytes = 32 bytes
    uint32_t timestamp;          // 4 bytes (ms since boot)
} NeuralData;
```

**DeviceStatus** (12 bytes - `inc/device_status.h`):
```c
typedef struct {
    uint8_t battery_level;      // 0-100%
    int8_t temperature;         // Celsius
    bool recording_status;      // Active/inactive
    char configuration[9];      // Config string
} DeviceStatus;
```

### Dual-Consumer FIFO
The FIFO (`src/fifo_buffer.c`) supports two independent readers with separate read indices:
- **BLE priority (0):** Low-latency streaming, 10-sample threshold
- **SD priority (1):** Bulk writes, 200-sample threshold (128 structs = 4608 bytes, multiple of 512)
- On overflow, discards data from the lower-priority consumer
- Thread-safe via mutex; semaphore signals when buffer reaches 50% fill

### Threading Model
| Thread | Priority | Stack | Purpose |
|--------|----------|-------|---------|
| intan_thread | 0 | 2KB | ADC sampling via work queue timer |
| sd_card_writer_thread | 3 | 4KB | Writes FIFO data to SD in 128-struct chunks |
| neural_data_notify_thread | 4 | 2KB | BLE notifications, polls for new data |
| status_notify_thread | 8 | 1KB | Device status updates every 1 second |

### Initialization Sequence (main.c)
1. 100ms stabilization delay
2. SD card init + mount filesystem + create session folder
3. BLE init + start advertising
4. Wait for BLE connection (negotiates 2M PHY, max MTU 244, min interval 7.5ms)
5. FIFO buffer init
6. Intan RHD2132 init (SPI verify + register config + calibration)
7. Spawn threads in reverse priority order

## Source Files Reference

| File | Lines | Purpose |
|------|-------|---------|
| `src/main.c` | ~130 | Entry point, initialization, thread creation |
| `src/ble_module.c` | ~405 | BLE stack init, advertising, connection callbacks, PHY/MTU negotiation |
| `src/neuralbs.c` | ~120 | Custom GATT service with 2 characteristics (neural data + status) |
| `src/fifo_buffer.c` | ~210 | Circular buffer, dual readers, priority overflow, mutex protection |
| `src/intan.c` | ~310 | SPI driver, RHD2132 register config, ADC sampling, work queue handler |
| `src/sd_card.c` | ~650 | FAT filesystem ops, session folder management, write-from-FIFO thread |
| `src/fakedata_module.c` | ~75 | Test data generator with configurable patterns (currently disabled) |

## Hardware Configuration

### Device Tree Overlay (`boards/nrf52840dk_nrf52840.overlay`)
- **SPI1:** Intan RHD2132 @ 20 MHz, CS on GPIO0 pin 29
- **SPI3:** SD card via SDMMC @ 20 MHz, CS on GPIO1 pin 12
- QSPI and PWM0 disabled to free resources

### Intan RHD2132 Specifications
- 32-channel neural amplifier (16 used)
- ADC scale: 0.195 µV/bit (RHD2000 standard)
- Programmable bandwidth: 1 Hz - 300 Hz (narrow band for LFPs)
- Max sampling: 30 kS/s/channel; tested stable up to 750 Hz in BLE+SD mode
- SPI command protocol: 16-bit commands, 2-command pipeline delay
- Input-referred noise: 2.4 µVrms

### BLE Service UUIDs (`inc/neuralbs.h`)
- **Service:** `ac9a900b-d5c2-4eea-a18b-c30efc00d25e`
- **Neural Data Characteristic:** `bcd5243f-0607-4899-afda-999999999999` (Notify, Read)
- **Device Status Characteristic:** `d3171a00-57e9-476d-a6db-111111111111` (Notify)

### BLE Performance
- Max payload: 244 bytes = 6 NeuralData samples per notification
- Connection interval: 7.5ms minimum (LLPM)
- Stable BLE streaming: up to 100 Hz sampling
- Stable SD logging: up to 750 Hz sampling

## Code Conventions

### Naming
- **Types:** PascalCase (`NeuralData`, `DeviceStatus`, `fifo_buffer_t`)
- **Functions:** snake_case (`init_fifo_buffer()`, `ble_wait_for_connection()`)
- **Macros:** UPPERCASE (`FIFO_BUFFER_SIZE`, `MAX_CHANNELS`, `BLE_PAYLOAD_MAX`)
- **Thread data:** `k_thread module_thread_data`
- **Stacks:** `K_THREAD_STACK_DEFINE(module_stack, STACK_SIZE)`

### File Headers
All source files start with:
```c
// Marmoset FMW V0
// module_name.c
```

### Thread Definitions
```c
k_thread module_thread_data;
K_THREAD_STACK_DEFINE(module_stack, STACK_SIZE);
void module_thread(void *arg1, void *arg2, void *arg3);
```

### Error Handling
- Return `int` with errno values (-EINVAL, -ENODEV, -EPERM)
- Zephyr logging: `LOG_INF()`, `LOG_ERR()`, `LOG_WRN()`
- Module registration: `LOG_MODULE_REGISTER(module_name, LOG_LEVEL_INF)`

### Include Guards
```c
#ifndef MODULE_NAME_H
#define MODULE_NAME_H
// ...
#endif /* MODULE_NAME_H */
```

## Python Scripts (`scripts/`)

### Data Processing Pipeline

| Script | Purpose |
|--------|---------|
| `binarydecoder.py` | Convert binary `data_*.bin` files to CSV (36 bytes/sample) |
| `process_neural_data.py` | Advanced DSP: multi-threaded decoding, snippet extraction, visualization |
| `process_neural_data2.py` | Alternative processing with different analysis options |
| `plot_neural_data.py` | Time-series visualization of all 16 channels |
| `large_plotter.py` | Memory-efficient plotting for large datasets |
| `plot_attribute_deviation.py` | Analyze BLE connection interval deviations |
| `test_ble_log_decoding.py` | Parse and validate BLE notification logs |

### Power Analysis (`scripts/poweranalyser/`)
- `process_power_data.py` - Analyzes Agilent N6705B DC Power Analyzer CSV exports
- Generates time-series plots, power bar charts, breakdown analysis
- Maps experiment files (1-16.csv) to configurations (BLE+SD, BLE Only, SD Only, Idle)

### Binary Data Format
- 36 bytes per sample: 16 × int16_t (channels) + uint32_t (timestamp)
- Little-endian packed (`<16h` for channels, `<I` for timestamp)
- ADC conversion: `raw_value × 0.195 = µV`
- Timestamp in milliseconds since device boot

### Usage Example
```bash
# Convert binary session to CSV
python scripts/binarydecoder.py scripts/f_session_1/ output.csv

# Process and visualize neural data
python scripts/process_neural_data.py --input_folder scripts/session_files/home-tests/session_44/
```

## Test Data (`scripts/`)

### Recording Sessions
| Location | Description |
|----------|-------------|
| `f_session_1/`, `f_session_3/` | Fake data generator test sessions |
| `session_files/home-tests/` | Real recording sessions (session_44-51) |
| `session_files/pre-DSP/` | Recordings before signal processing improvements |
| `session_files/past_recordings/` | Historical test data |

### Throughput Tests (`scripts/throughputdata/`)
- `b1.txt` - `b15.txt`: BLE throughput logs at various sampling rates
- `fb1.txt` - `fb9.txt`: Fakedata + BLE throughput tests
- `f_session_1/` - `f_session_9/`: Session data at different configurations
- Used to generate throughput efficiency and packet loss analysis

### Power Consumption Data (`scripts/poweranalyser/`)
- `1.csv` - `16.csv`: Raw power measurements from Agilent N6705B
- Experiment mapping: 1-5 (BLE+SD), 6-10 (SD Only), 11-15 (BLE Only), 16 (Idle)
- Sampling frequencies: 1000, 500, 250, 100, 20 Hz

## Hardware Design Files

### 3D CAD Models (`Mech-Electro/MARM-CAD/`)
| File | Description |
|------|-------------|
| `V1-Assembly v103.step` | Initial housing design - exploratory PCB support geometry |
| `V2-Assembly v30.step` | Added battery cover, 2-screw removal, ~8.5g |
| `V3-Assembly v43.step` | Current design - quick-swap battery, 1 screw, wall cutouts |
| `V4-Assembly v28.step` | Final iteration - 6.5g housing, 28x38.1x29.2mm |

**Design Evolution:**
- V1-V2: Stacked supports, XYZ constraints, front-mounted screws (antenna interference)
- V3-V4: Crown shelving, single PCB support, reduced parts (6 total), 0.5mm min wall thickness
- Material: Visijet M2S-HT90 (SLA 3D printing, biocompatible, high-strength)

### PCB Design (`Mech-Electro/PCB/`)
| File | Description |
|------|-------------|
| `Marmoset-PCB-DuBo.zip` | Bottom board: ISP1807 MCU module, RHD2132, Omnetics connector |
| `Marmoset-PCB-DuTop.zip` | Top board: SD card holder, LP5907 voltage regulator, power switch, battery connector |

**PCB Stack:**
- Dual-board stacked design connected via micro-header pins
- Bottom board: 13.9 × 25 mm (MCU + ADC + electrode connector)
- Top board: 16.1 × 27.6 mm (power + storage)
- Combined PCB weight: ~4.52g

## Project Documentation (`Presentations-Reports/`)

| File | Description |
|------|-------------|
| `MartinLombard_Final_Report_v3.pdf` | **Complete MSc thesis** (43 pages): Literature review, system design, firmware architecture, PCB design, mechanical design, evaluation results, power analysis |
| `MARM-VIVA.pdf` | **Presentation slides** (54MB): Visual overview of project, results, demonstrations |
| `final-MarmosetProject-PlanningReport-MartinLombard.pdf` | **Planning document**: Initial requirements, timeline, risk assessment |

### Key Results from Final Report
- **Throughput:** Stable 750 Hz SD, 100 Hz BLE (12,000 total S/s)
- **Latency:** Sub-10ms BLE transmission
- **Power:** 30mA @ 500Hz (BLE+SD), ~2h 40min autonomy with 80mAh battery
- **Weight:** 11.05g total (3% of marmoset body weight)
- **Dimensions:** 25.2 × 38.1 × 20 mm
- **Battery swap:** 30 seconds

## Legacy Code (`_oldsrc/`)

| File | Description |
|------|-------------|
| `main_old.c` | Previous main.c iteration |
| `my_lbs.c`, `my_lbs.h` | Original LED Button Service (replaced by neuralbs) |
| `sd_card_.c` | Early SD card driver attempt |

## Current Development Status

### Completed
- SD card driver and FAT filesystem integration
- Dual-reader FIFO buffer with priority overflow
- Intan RHD2132 SPI driver and ADC configuration
- BLE GATT service with neural + status characteristics
- Low-latency BLE connection parameter negotiation (LLPM)
- Multi-threaded data pipeline
- Python data processing and visualization tools

### In Progress (`notes.md`)
- Semaphore implementation to prevent FIFO overflow at high rates
- Neural network for gap-filling missing BLE packets

### Known Limitations
- SD card initialization currently commented out in main.c (BLE-only for initial in-vivo tests)
- Fakedata thread disabled in production
- BLE throughput limited to ~100 Hz stable; higher rates cause packet loss
- Remote operation (BLE commands) not yet implemented

## Common Tasks

### Adding a New BLE Characteristic
1. Define UUID in `inc/neuralbs.h`
2. Add characteristic to GATT service in `src/neuralbs.c` using `BT_GATT_CHARACTERISTIC`
3. Implement read/notify callbacks following existing patterns
4. Add notification function similar to `nbs_send_neural_data_notification()`

### Modifying FIFO Behavior
- Buffer size: `FIFO_BUFFER_SIZE` in `inc/fifo_buffer.h`
- Priority constants: `FIFO_PRIORITY_BLE` / `FIFO_PRIORITY_SD`
- SD write threshold: Adjust `SD_WRITE_THRESHOLD` (default 128 structs)
- Semaphore threshold: `FIFO_SEM_THRESHOLD` (50% fill trigger)

### Changing BLE Parameters
- Connection intervals: `CONFIG_BT_PERIPHERAL_PREF_MIN/MAX_INT` in `prj.conf`
- Data length: `CONFIG_BT_CTLR_DATA_LENGTH_MAX` in `prj.conf`
- Device name: `CONFIG_BT_DEVICE_NAME` in `prj.conf`
- MTU: Negotiated in `ble_module.c` connection callback

### Adjusting Intan Sampling
- Sampling rate: Timer period in `src/intan.c` work queue setup
- Bandwidth filter: RHD2132 register configuration in `intan_configure_registers()`
- Channel selection: Modify ADC read commands in `RHD_handler()`

### Processing Recorded Data
```bash
# Convert binary session to CSV
python scripts/binarydecoder.py <session_folder> <output.csv>

# Analyze power consumption
cd scripts/poweranalyser
python process_power_data.py *.csv
```

## Important Notes for AI Assistants

1. **This is embedded firmware** - changes affect real hardware on live animals. Test thoroughly with fakedata module first.

2. **Thread safety is critical** - FIFO operations are mutex-protected. Always acquire `fifo->mutex` before accessing buffer state.

3. **Memory is limited** - nRF52840 has 256KB RAM. Current FIFO uses 10.8KB (300 × 36 bytes). Monitor stack usage with `CONFIG_THREAD_STACK_INFO`.

4. **BLE payload limit** - 244 bytes max = 6 NeuralData samples per notification. Don't exceed MTU.

5. **SD card write optimization** - Always write multiples of 512 bytes for best performance. Current: 4608 bytes (128 samples).

6. **Intan SPI protocol** - Commands have 2-sample pipeline delay. First response contains data from 2 commands ago.

7. **Zephyr conventions** - Follow Zephyr RTOS patterns for threads, work queues, and logging. Use `K_THREAD_DEFINE` for static threads.

8. **Power considerations** - SD card operations dominate power consumption. BLE-only mode extends battery life significantly.

9. **Connection parameters** - LLPM (Low-Latency Peripheral Mode) requires Nordic SoftDevice Controller. Don't change to Zephyr BLE controller.

10. **Animal welfare** - Device weight impacts marmoset behavior. Any changes affecting weight or dimensions should be flagged.

## Performance Benchmarks

| Configuration | Max Stable Rate | Power Draw | Autonomy (80mAh) |
|---------------|-----------------|------------|------------------|
| BLE+SD | 750 Hz (SD), 100 Hz (BLE) | 30 mA | 2h 40min |
| BLE Only | 100 Hz | 16.6 mA | ~5 hours |
| SD Only | 750 Hz | 24.8 mA | ~3.2 hours |
| Idle | - | 12.9 mA | ~6.2 hours |
