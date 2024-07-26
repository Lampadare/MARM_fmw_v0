// intan.c

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "../inc/intan.h"
#include "../inc/neural_data.h"
#include "../inc/fifo_buffer.h"

LOG_MODULE_REGISTER(intan_tests, LOG_LEVEL_DBG);

// Define types
typedef unsigned char u8_t;
typedef unsigned short u16_t;

// ADC COMMANDS =================================================================================
// CONVERT (16 channels + 3 dummy command(read ROM 63)(user define) |||| 0x3f00 -> CONVERT(63))
static u16_t RHD_CONVERT[19] = {0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700,
                                0x0800, 0x0900, 0x0a00, 0x0b00, 0x0c00, 0x0d00, 0x0e00, 0x0f00, 0xFF00, 0xFF00, 0xFF00};
static u16_t T_result[19];
// SELF CALIBRATION (followed by 9 dummys as necessary)
#define CALIBRATE 0x5500
static const u16_t NINE_DUMMPY[9] = {0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00};
// CLEAR
#define CLEAR 0x6A00 // not necessary to use this command

// REGISTER CONFIGURATION =================================================================================
#define Register0 0x80DE // amp fast settle is 0 ,20-150HZ filter ,enable ADC AND amp(disable is OK)
#define Register1 0x8120 // VDD sense disable ,using 16 * 1KS/s ADC
#define Register2 0x8228 // MUX bias current, configuration as above
#define Register3 0x8302 // diable tempS and digout
#define Register4 0x84B0 // absmode enable + unsigned ADC + weak MISO + DSP high-pass filter enable(differentiator)
// Impedance check
#define Register5 0x8500 // Impedance check control ,which is disable
#define Register6 0x8600 // DAC output voltage ,there is 0
#define Register7 0x8700 // Impedance check electrode select, this is 0
// on-chip Amplifier bandwidth Select
#define Register8 0x882C //  using 20-150 Hz bandwidth
#define Register9 0x8911
#define Register10 0x8a08
#define Register11 0x8b15
#define Register12 0x8c36
#define Register13 0x8d00
// indicidual Amplifier Power ,all set to one for using all channels' Amplifier
#define Register14 0x8eff
#define Register15 0x8fff
#define Register16 0x90ff
#define Register17 0x91ff

// SPI RHD cs ================================================================================
#define SPIOP SPI_WORD_SET(8) | SPI_TRANSFER_MSB
struct spi_dt_spec spispec = SPI_DT_SPEC_GET(DT_NODELABEL(rhd2232), SPIOP, 0);

// timer =======================================================================================
struct k_timer RHD_timer;
extern void my_timer_handler(struct k_timer *timer_id); // cb function call
#define SAMPLE_RATE_HZ 130
int64_t start_time = 0; // Record the start time for timestamp calculations

// RHD_CS ================================================================================
bool RHD_SAMPLE = true;
int ret;
int RHD_err;
bool RHD_init = false;

// define thread ================================================================================
K_THREAD_STACK_DEFINE(intan_stack, INTAN_THREAD_STACK_SIZE); // Define the stack for the intan thread
struct k_thread intan_thread_data;                           // Declare the thread data structure for the intan thread

// define the work for the handler =======================================================================================
struct rhd_work_data
{
    struct k_work work;
    fifo_buffer_t *fifo_buffer;
};
static struct rhd_work_data rhd_work;

// SPI initialisation ================================================================================
static void spi_init(void)
{
    int err = spi_is_ready_dt(&spispec);
    if (!err)
    {
        LOG_ERR("Error: SPI device is not ready, err: %d", err);
        return 0;
    }
    else
    {
        LOG_INF("SPI device is ready");
    }
}

// spi buffer structs ================================================================================
static u8_t tx_buffer[2];
static u8_t rx_buffer[2];
const struct spi_buf tx_buf = {
    .buf = tx_buffer,
    .len = sizeof(tx_buffer)};
struct spi_buf rx_buf = {
    .buf = rx_buffer,
    .len = sizeof(rx_buffer),
};
const struct spi_buf_set tx = { // tx buffer SET
    .buffers = &tx_buf,
    .count = 1};
const struct spi_buf_set rx = { // rx buffer SET
    .buffers = &rx_buf,
    .count = 1};

// SPI trans function ================================================================================
u16_t spi_trans(u16_t command)
{
    int err;
    tx_buffer[0] = (command >> 8) & 0xFF; // High byte
    tx_buffer[1] = command & 0xFF;        // Low byte

    err = spi_transceive_dt(&spispec, &tx, &rx);
    if (err)
    {
        LOG_ERR("spi_transceive_dt() failed, err: %d", err);
        return err;
    }

    return (rx_buffer[0] << 8) | rx_buffer[1];
}

void reset_cs(void)
{
    // Pull CS high
    gpio_pin_set_dt(&spispec.config.cs.gpio, 1);
    k_busy_wait(1); // Wait for 1 microsecond
    // Pull CS low
    gpio_pin_set_dt(&spispec.config.cs.gpio, 0);
}

void init_spi_pipeline(void)
{
    reset_cs();
    // Send six dummy READ commands to prime the pipeline
    for (int i = 0; i < 12; i++)
    {
        u16_t result = spi_trans(0xC000); // READ from register 0
        LOG_INF("Dummy read %d result: 0x%04X", i, result);
    }
}

u16_t spi_trans_wait(u16_t command)
{
    spi_trans(command);
    spi_trans(0xC000);        // Dummy read
    return spi_trans(0xC000); // Another dummy read, returns the result of the original command
}

bool spi_check(void)
{
    char company[5] = {0};
    u16_t results[5];

    // Send READ commands
    for (int i = 0; i < 5; i++)
    {
        u16_t read_command = 0xC000 | ((i + 40) << 8); // Start from register 40
        results[i] = spi_trans_wait(read_command);
        LOG_INF("Read command 0x%04X, result: 0x%04X", read_command, results[i]);
        k_sleep(K_MSEC(10)); // Short delay between reads
    }

    // Process results
    for (int i = 0; i < 5; i++)
    {
        company[i] = (char)(results[i] & 0xFF);
        LOG_INF("ROM Register %d: 0x%04X (ASCII: %c)", i + 40, results[i], company[i]);
    }

    // Check if the result spells "INTAN"
    return (strncmp(company, "INTAN", 5) == 0);
}

// RHD init function ================================================================================
static int RHD2232_init(void)
{
    static const u16_t Register_config[18] = {Register0, Register1, Register2, Register3, Register4, Register5, Register6, Register7,
                                              Register8, Register9, Register10, Register11, Register12, Register13, Register14, Register15,
                                              Register16, Register17};
    static u16_t config_results[18];
    static u16_t result;
    int err;
    u16_t calibrate_result;

    printk("Testing basic SPI communication:\n");

    // Initialize the SPI pipeline
    init_spi_pipeline();

    // Send CLEAR command ------------------------------------------------------------
    result = spi_trans_wait(0x6A00);
    LOG_INF("CLEAR command result with WAIT: 0x%04X", result);

    // Read "INTAN" from ROM to make sure things are working ------------------------------------------------------------
    if (spi_check())
    {
        LOG_INF("SPI check passed, 'INTAN' read successfully");
    }
    else
    {
        LOG_ERR("SPI check failed, 'INTAN' not read correctly");
        return 1;
    }

    k_sleep(K_SECONDS(1));

    // RHD write to registers ------------------------------------------------------------
    for (int i = 0; i < sizeof(Register_config) / 2; i++)
    {
        result = spi_trans_wait(Register_config[i]);
        LOG_INF("Write command: 0x%04X, Result: 0x%04X", Register_config[i], result);

        // Check if the upper byte is all ones and the lower byte matches the sent data
        if ((result & 0xFF00) != 0xFF00 || (result & 0x00FF) != (Register_config[i] & 0x00FF))
        {
            LOG_ERR("Write failed for register %d", i);
            return 4; // Error code for write failure
        }
    }
    LOG_INF("All writes successful");

    // CALIBRATE ------------------------------------------------------------
    k_sleep(K_MSEC(1)); // 100us delay before ADC calibrate
    calibrate_result = spi_trans(CALIBRATE);

    // DUMMY COMMANDS POST CALIBRATE ------------------------------------------------------------
    for (int j = 0; j < 9; j++)
    {
        calibrate_result = spi_trans(NINE_DUMMPY[j]);
        LOG_INF("calibrate_result[%d]: 0x%04X", j, calibrate_result);
    }

    spi_trans(0xC000); // Dummy read
    calibrate_result = spi_trans(0xC000);
    LOG_INF("CALIBRATE done, calibrate_result: 0x%04X", calibrate_result);

    return 0;
}

// timer cb ================================================================================
void RHD_handler(struct k_work *work)
{
    struct rhd_work_data *work_data = CONTAINER_OF(work, struct rhd_work_data, work);
    fifo_buffer_t *fifo_buffer = work_data->fifo_buffer;

    uint64_t stamp = k_uptime_get_32();
    NeuralData sample;

    // Sample all channels
    for (int i = 0; i < 19; i++)
    {
        T_result[i] = spi_trans(RHD_CONVERT[i]);
    }

    // Record only channels 2-17 (16 channels) into the sample
    for (int i = 2; i < 18; i++)
    {
        sample.channel_data[i - 2] = T_result[i];
    }
    sample.timestamp = (uint32_t)(stamp - start_time);

    // Write the sample to the FIFO buffer
    size_t samples_written = write_to_fifo_buffer(fifo_buffer, &sample, 1);
    if (samples_written != 1)
    {
        LOG_ERR("Failed to write neural data to FIFO buffer.");
    }

    // Update the global latest_neural_data
    latest_neural_data.data = sample;
    latest_neural_data.sent = false; // Mark new data as not sent

    int64_t delta = k_uptime_delta(&stamp);
    // LOG_INF("SPI Use time is: %lld ms", delta);
}

#define INIT_RHD_WORK(work_data_ptr, fifo_buffer_ptr)     \
    do                                                    \
    {                                                     \
        k_work_init(&(work_data_ptr)->work, RHD_handler); \
        (work_data_ptr)->fifo_buffer = fifo_buffer_ptr;   \
    } while (0)

// timer cb ================================================================================
void my_timer_handler(struct k_timer *dummy)
{
    k_work_submit(&rhd_work.work);
}

// main function loop ================================================================================
void intan_thread(void *arg1, void *arg2, void *arg3)
{
    fifo_buffer_t *fifo_buffer = (fifo_buffer_t *)arg1; // Cast the argument to the appropriate fifo_buffer_t type
    NeuralData data;                                    // Declare the data structure to hold neural data
    uint16_t counter[MAX_CHANNELS] = {0};               // Initialize counters for each channel
    start_time = k_uptime_get();                        // Record the start time for timestamp calculations

    // Check nodes --------------------------------------------------------------------------------
    LOG_INF("==================");
    LOG_INF("Intan thread starting...");
    LOG_INF("Checking nodes...");
    LOG_INF("spi2 %s", DT_NODE_HAS_STATUS(DT_NODELABEL(spi2), okay) ? "found" : "not found");
    LOG_INF("RHD2232 %s", DT_NODE_HAS_STATUS(DT_NODELABEL(rhd2232), okay) ? "found" : "not found");
    LOG_INF("==================");

    // timer init --------------------------------------------------------------------------------
    INIT_RHD_WORK(&rhd_work, fifo_buffer);
    k_timer_init(&RHD_timer, my_timer_handler, NULL); // init timer

    // SPI init --------------------------------------------------------------------------------
    LOG_INF("SPIM Example\n");
    spi_init();

    // RHD init --------------------------------------------------------------------------------
    int retry_count = 0;
    const int max_retries = 5; // Set a maximum number of retries to avoid infinite loops
    while (!RHD_init)          // Infinite loop
    {
        RHD_err = RHD2232_init();
        if (RHD_err == 0)
        {
            LOG_INF("RHD2232 init success");
            RHD_init = true;
            break; // Exit the loop on success
        }

        retry_count++;
        LOG_ERR("RHD2232 init failed with error %d (Attempt %d of %d)", RHD_err, retry_count, max_retries);

        if (retry_count >= max_retries)
        {
            LOG_ERR("Max retries reached. Initialization failed.");
            return;
        }

        k_sleep(K_MSEC(1000)); // Wait for 1 second before retrying
    }

    // timer start --------------------------------------------------------------------------------
    k_timer_start(&RHD_timer, K_SECONDS(3), K_USEC(1000000 / SAMPLE_RATE_HZ)); // 130kHz and starts after 3 seconds

    // main loop --------------------------------------------------------------------------------
    while (true)
    {
        LOG_INF("=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=");
        for (int i = 0; i < 19; i++)
        {
            LOG_INF("Channel [%d]: 0x%04X (decimal: %d)", i, T_result[i], T_result[i]); // 2-17 are the actual channels
        }
        LOG_INF("=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=");
        k_sleep(K_MSEC(10000));
    }
}