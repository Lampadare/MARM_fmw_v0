// intan.c

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "../inc/intan.h"
#include "../inc/neural_data.h"
#include "../inc/fifo_buffer.h"

LOG_MODULE_REGISTER(intan_tests, LOG_LEVEL_DBG);

#define INTAN_WORK_Q_STACK_SIZE 2048
#define INTAN_WORK_Q_PRIORITY 0 // Highest priority, even above the INTAN_THREAD_PRIORITY

K_THREAD_STACK_DEFINE(intan_work_q_stack, INTAN_WORK_Q_STACK_SIZE);
static struct k_work_q intan_work_q;

#define SAMPLE_RATE_HZ 130
#define CHANNEL_COUNT 16
#define COMMAND_COUNT (CHANNEL_COUNT + 3)

// ADC commands
static uint16_t RHD_CONVERT[COMMAND_COUNT] = {
    0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700,
    0x0800, 0x0900, 0x0a00, 0x0b00, 0x0c00, 0x0d00, 0x0e00, 0x0f00,
    0xFF00, 0xFF00, 0xFF00};
static uint16_t T_result[COMMAND_COUNT];

#define CALIBRATE 0x5500
#define CLEAR 0x6A00

// Register configuration
#define Register0 0x80DE  // Keep as is
#define Register1 0x8120  // Set ADC buffer bias to 32 (0x20)
#define Register2 0x8228  // Set MUX bias to 40 (0x28)
#define Register3 0x8302  // Keep as is
#define Register4 0x84B0  // Keep as is, but consider enabling DSP (see note below)
#define Register5 0x8500  // Keep as is
#define Register6 0x8600  // Keep as is
#define Register7 0x8700  // Keep as is
#define Register8 0x882C  // Set RH1 DAC1 to 44 (0x2C)
#define Register9 0x8911  // Set RH1 DAC2 to 17 (0x11)
#define Register10 0x8A08 // Set RH2 DAC1 to 8 (0x08)
#define Register11 0x8B15 // Set RH2 DAC2 to 21 (0x15)
#define Register12 0x8C10 // Set RL DAC1 to 16 (0x10)
#define Register13 0x8D3C // Set RL DAC2 to 60 (0x3C) and RL DAC3 to 1 (0x01)
#define Register14 0x8EFF // Keep as is
#define Register15 0x8FFF // Keep as is
#define Register16 0x90FF // Keep as is
#define Register17 0x91FF // Keep as is

// SPI configuration
#define SPIOP SPI_WORD_SET(8) | SPI_TRANSFER_MSB
struct spi_dt_spec spispec = SPI_DT_SPEC_GET(DT_NODELABEL(rhd2232), SPIOP, 0);

// Timer and thread configuration
struct k_timer RHD_timer;
K_THREAD_STACK_DEFINE(intan_stack, INTAN_THREAD_STACK_SIZE);
struct k_thread intan_thread_data;

// Globals
static int64_t start_time = 0;
static bool RHD_init = false;
static struct rhd_work_data
{
    struct k_work work;
    fifo_buffer_t *fifo_buffer;
} rhd_work;

// Function prototypes
static void spi_init(void);
static uint16_t spi_trans(uint16_t command);
static uint16_t spi_trans_wait(uint16_t command);
static bool spi_check(void);
extern int intan_init(void);
static void RHD_handler(struct k_work *work);
static void my_timer_handler(struct k_timer *dummy);

// SPI initialization
static void spi_init(void)
{
    if (!spi_is_ready_dt(&spispec))
    {
        LOG_ERR("SPI device is not ready");
        return;
    }
    LOG_INF("SPI device is ready");
}

// SPI transaction function
static uint16_t spi_trans(uint16_t command)
{
    uint8_t tx_buffer[2] = {(command >> 8) & 0xFF, command & 0xFF};
    uint8_t rx_buffer[2];
    const struct spi_buf tx_buf = {.buf = tx_buffer, .len = sizeof(tx_buffer)};
    const struct spi_buf rx_buf = {.buf = rx_buffer, .len = sizeof(rx_buffer)};
    const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
    const struct spi_buf_set rx = {.buffers = &rx_buf, .count = 1};

    if (spi_transceive_dt(&spispec, &tx, &rx))
    {
        LOG_ERR("SPI transaction failed");
        return 0;
    }
    return (rx_buffer[0] << 8) | rx_buffer[1];
}

// SPI transaction with wait
static uint16_t spi_trans_wait(uint16_t command)
{
    spi_trans(command);
    spi_trans(0xC000);        // Dummy read
    return spi_trans(0xC000); // Another dummy read, returns the result of the original command
}

// SPI check function
static bool spi_check(void)
{
    char company[5] = {0};
    for (int i = 0; i < 5; i++)
    {
        uint16_t result = spi_trans_wait(0xC000 | ((i + 40) << 8));
        company[i] = (char)(result & 0xFF);
        LOG_INF("ROM Register %d: 0x%04X (ASCII: %c)", i + 40, result, company[i]);
    }
    return (strncmp(company, "INTAN", 5) == 0);
}

// RHD initialization function
static int intan_init(void)
{
    static const uint16_t Register_config[18] = {Register0, Register1, Register2, Register3, Register4, Register5, Register6, Register7,
                                                 Register8, Register9, Register10, Register11, Register12, Register13, Register14, Register15,
                                                 Register16, Register17};

    // Initialize SPI pipeline
    for (int i = 0; i < 12; i++)
    {
        spi_trans(0xC000);
    }

    // Send CLEAR command
    spi_trans_wait(CLEAR);

    // Check SPI communication
    if (!spi_check())
    {
        LOG_ERR("SPI check failed");
        return 1;
    }

    // Write to registers
    for (int i = 0; i < 18; i++)
    {
        uint16_t result = spi_trans_wait(Register_config[i]);
        if ((result & 0xFF00) != 0xFF00 || (result & 0x00FF) != (Register_config[i] & 0x00FF))
        {
            LOG_ERR("Write failed for register %d", i);
            return 4;
        }
    }

    // Calibrate
    spi_trans(CALIBRATE);
    for (int j = 0; j < 9; j++)
    {
        spi_trans(0xFF00);
    }
    spi_trans(0xC000);
    uint16_t calibrate_result = spi_trans(0xC000);
    LOG_INF("CALIBRATE done, calibrate_result: 0x%04X", calibrate_result);

    LOG_INF("RHD2232 initialization complete");
    return 0;
}

// RHD handler function
static void RHD_handler(struct k_work *work)
{
    struct rhd_work_data *work_data = CONTAINER_OF(work, struct rhd_work_data, work);
    fifo_buffer_t *fifo_buffer = work_data->fifo_buffer;
    NeuralData sample;

    uint64_t stamp = k_uptime_get_32();

    // Sample all channels
    for (int i = 0; i < COMMAND_COUNT; i++)
    {
        T_result[i] = spi_trans(RHD_CONVERT[i]);
    }

    // Record channels 2-17 (16 channels) into the sample
    for (int i = 2; i < 18; i++)
    {
        sample.channel_data[i - 2] = T_result[i];
    }
    sample.timestamp = (uint32_t)(stamp - start_time);

    // Write the sample to the FIFO buffer
    if (write_to_fifo_buffer(fifo_buffer, &sample, 1) != 1)
    {
        LOG_ERR("Failed to write neural data to FIFO buffer.");
    }

    // Update the global latest_neural_data
    latest_neural_data.data = sample;
    latest_neural_data.sent = false;
}

// Timer handler
void my_timer_handler(struct k_timer *dummy)
{
    k_work_submit_to_queue(&intan_work_q, &rhd_work.work);
}

// Main intan thread function
void intan_thread(void *arg1, void *arg2, void *arg3)
{
    fifo_buffer_t *fifo_buffer = (fifo_buffer_t *)arg1;
    start_time = k_uptime_get();

    LOG_INF("Intan thread starting...");
    LOG_INF("spi2 %s", DT_NODE_HAS_STATUS(DT_NODELABEL(spi2), okay) ? "found" : "not found");
    LOG_INF("RHD2232 %s", DT_NODE_HAS_STATUS(DT_NODELABEL(rhd2232), okay) ? "found" : "not found");

    // Initialize the custom work queue with even higher priority
    k_work_queue_init(&intan_work_q);
    k_work_queue_start(&intan_work_q, intan_work_q_stack,
                       K_THREAD_STACK_SIZEOF(intan_work_q_stack),
                       INTAN_WORK_Q_PRIORITY, NULL);

    // Initialize work, timer, and SPI
    k_work_init(&rhd_work.work, RHD_handler);
    rhd_work.fifo_buffer = fifo_buffer;
    k_timer_init(&RHD_timer, my_timer_handler, NULL);
    spi_init();

    // Initialize RHD2232
    for (int retry_count = 0; retry_count < 5; retry_count++)
    {
        if (intan_init() == 0)
        {
            RHD_init = true;
            break;
        }
        LOG_ERR("RHD2232 init failed (Attempt %d of 5)", retry_count + 1);
        k_sleep(K_MSEC(1000));
    }

    if (!RHD_init)
    {
        LOG_ERR("Max retries reached. Initialization failed.");
        return;
    }

    // Start timer
    k_timer_start(&RHD_timer, K_SECONDS(3), K_USEC(1000000 / SAMPLE_RATE_HZ));

    // // Main loop
    // while (true)
    // {
    //     LOG_INF("=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=");
    //     for (int i = 0; i < COMMAND_COUNT; i++)
    //     {
    //         LOG_INF("Channel [%d]: 0x%04X (decimal: %d)", i, T_result[i], T_result[i]);
    //     }
    //     LOG_INF("=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=");
    //     k_sleep(K_MSEC(10000));
    // }
}