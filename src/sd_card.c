#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/fs/fs.h>
#include <ff.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/debug/stack.h>
#include <stdio.h>

#include <zephyr/logging/log.h>
#include "../inc/neural_data.h"
#include "../inc/sd_card.h"
#include "../inc/fifo_buffer.h"

LOG_MODULE_REGISTER(sd_card, LOG_LEVEL_DBG); // different in sample code, was CONFIG_MODULE_SD_CARD_LOG_LEVEL, for module specific Kconfig files

#define SD_ROOT_PATH "/SD:/"
#define PATH_MAX_LEN 260
#define K_SEM_OPER_TIMEOUT_MS 1000

K_SEM_DEFINE(m_sem_sd_oper_ongoing, 1, 1);

static const char *sd_root_path = SD_ROOT_PATH;
static FATFS fat_fs;
static bool sd_init_success;

static struct fs_mount_t mnt_pt = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
};

static char current_data_folder[PATH_MAX_LEN + 1];

#define WRITE_INTERVAL_MS 500
#define MAX_FILE_SIZE (76128)     // 76 KB - equivalent to 2.4 seconds recording (including timestamps)
#define WRITE_BUFFER_SIZE (25376) // 25 KB write buffer (0.8 second of recording)
#define MAX_NEURAL_DATA_PER_WRITE 100

K_THREAD_STACK_DEFINE(sd_card_stack, SD_CARD_THREAD_STACK_SIZE);
struct k_thread sd_card_thread_data; // Declare the thread data structure for the fakedata thread

int sd_card_list_files(char const *const path, char *buf, size_t *buf_size)
{
    int ret;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;
    char abs_path_name[PATH_MAX_LEN + 1] = SD_ROOT_PATH;
    size_t used_buf_size = 0;

    printk("sd_card_list_files: taking sem");
    ret = k_sem_take(&m_sem_sd_oper_ongoing, K_MSEC(K_SEM_OPER_TIMEOUT_MS));
    if (ret)
    {
        LOG_ERR("Sem take failed. Ret: %d", ret);
        return ret;
    }

    printk("sd_card_list_files: sem taken");
    if (!sd_init_success)
    {
        k_sem_give(&m_sem_sd_oper_ongoing);
        return -ENODEV;
    }

    fs_dir_t_init(&dirp);

    if (path == NULL)
    {
        ret = fs_opendir(&dirp, sd_root_path);
        if (ret)
        {
            LOG_ERR("Open SD card root dir failed");
            k_sem_give(&m_sem_sd_oper_ongoing);
            return ret;
        }
    }
    else
    {
        if (strlen(path) > CONFIG_FS_FATFS_MAX_LFN)
        {
            LOG_ERR("Path is too long");
            k_sem_give(&m_sem_sd_oper_ongoing);
            return -FR_INVALID_NAME;
        }

        strcat(abs_path_name, path);

        ret = fs_opendir(&dirp, abs_path_name);
        if (ret)
        {
            LOG_ERR("Open assigned path failed");
            k_sem_give(&m_sem_sd_oper_ongoing);
            return ret;
        }
    }

    while (1)
    {
        ret = fs_readdir(&dirp, &entry);
        if (ret)
        {
            k_sem_give(&m_sem_sd_oper_ongoing);
            return ret;
        }

        if (entry.name[0] == 0)
        {
            break;
        }

        if (buf != NULL)
        {
            size_t remaining_buf_size = *buf_size - used_buf_size;
            ssize_t len = snprintk(
                &buf[used_buf_size], remaining_buf_size, "[%s]\t%s\n",
                entry.type == FS_DIR_ENTRY_DIR ? "DIR " : "FILE", entry.name);

            if (len >= remaining_buf_size)
            {
                LOG_ERR("Failed to append to buffer, error: %d", len);
                printk("Failed to append to buffer, error: %d", len);
                k_sem_give(&m_sem_sd_oper_ongoing);
                return -EINVAL;
            }

            used_buf_size += len;
        }

        LOG_INF("[%s] %s", entry.type == FS_DIR_ENTRY_DIR ? "DIR " : "FILE", entry.name);
    }

    ret = fs_closedir(&dirp);
    if (ret)
    {
        LOG_ERR("Close SD card root dir failed");
        printk("Close SD card root dir failed");
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    *buf_size = used_buf_size;
    k_sem_give(&m_sem_sd_oper_ongoing);
    return 0;
}

static char abs_path_name[PATH_MAX_LEN + 1];
static struct fs_file_t f_entry;

int sd_card_open_write_close(char const *const filename, char const *const data, size_t *size)
{
    int ret;

    LOG_INF("Entering sd_card_open_write_close");

    ret = k_sem_take(&m_sem_sd_oper_ongoing, K_MSEC(K_SEM_OPER_TIMEOUT_MS));
    if (ret)
    {
        LOG_ERR("Sem take failed. Ret: %d", ret);
        return ret;
    }

    if (!sd_init_success)
    {
        k_sem_give(&m_sem_sd_oper_ongoing);
        return -ENODEV;
    }

    // Construct the absolute path
    if (snprintf(abs_path_name, sizeof(abs_path_name), "%s", filename) >= sizeof(abs_path_name))
    {
        LOG_ERR("Filename is too long");
        k_sem_give(&m_sem_sd_oper_ongoing);
        return -ENAMETOOLONG;
    }

    LOG_INF("OWC: Writing to file: %s", abs_path_name);

    fs_file_t_init(&f_entry);

    LOG_INF("Opening file");
    ret = fs_open(&f_entry, abs_path_name, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
    if (ret)
    {
        LOG_ERR("Create file failed: %d", ret);
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    LOG_INF("Seeking to end of file");
    ret = fs_seek(&f_entry, 0, FS_SEEK_END);
    if (ret)
    {
        LOG_ERR("Seek file pointer failed: %d", ret);
        fs_close(&f_entry);
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    LOG_INF("Writing %zu bytes to file", *size);
    ret = fs_write(&f_entry, data, *size);
    if (ret < 0)
    {
        LOG_ERR("Write file failed: %d", ret);
        fs_close(&f_entry);
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    *size = ret;

    LOG_INF("Closing file");
    ret = fs_close(&f_entry);
    if (ret)
    {
        LOG_ERR("Close file failed: %d", ret);
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    k_sem_give(&m_sem_sd_oper_ongoing);
    LOG_INF("Exiting sd_card_open_write_close successfully");
    return 0;
}

int sd_card_open_read_close(char const *const filename, char *const buf, size_t *size)
{
    int ret;
    struct fs_file_t f_entry;
    char abs_path_name[PATH_MAX_LEN + 1] = SD_ROOT_PATH;

    ret = k_sem_take(&m_sem_sd_oper_ongoing, K_MSEC(K_SEM_OPER_TIMEOUT_MS));
    if (ret)
    {
        LOG_ERR("Sem take failed. Ret: %d", ret);
        return ret;
    }

    if (!sd_init_success)
    {
        k_sem_give(&m_sem_sd_oper_ongoing);
        return -ENODEV;
    }

    if (strlen(filename) > CONFIG_FS_FATFS_MAX_LFN)
    {
        LOG_ERR("Filename is too long");
        k_sem_give(&m_sem_sd_oper_ongoing);
        return -FR_INVALID_NAME;
    }

    strcat(abs_path_name, filename);
    fs_file_t_init(&f_entry);

    ret = fs_open(&f_entry, abs_path_name, FS_O_READ);
    if (ret)
    {
        LOG_ERR("Open file failed");
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    ret = fs_read(&f_entry, buf, *size);
    if (ret < 0)
    {
        LOG_ERR("Read file failed. Ret: %d", ret);
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    *size = ret;
    if (*size == 0)
    {
        LOG_WRN("File is empty");
    }

    ret = fs_close(&f_entry);
    if (ret)
    {
        LOG_ERR("Close file failed");
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    k_sem_give(&m_sem_sd_oper_ongoing);
    return 0;
}

int sd_card_open(char const *const filename, struct fs_file_t *f_seg_read_entry)
{
    int ret;
    char abs_path_name[PATH_MAX_LEN + 1] = SD_ROOT_PATH;
    size_t avilable_path_space = PATH_MAX_LEN - strlen(SD_ROOT_PATH);

    ret = k_sem_take(&m_sem_sd_oper_ongoing, K_MSEC(K_SEM_OPER_TIMEOUT_MS));
    if (ret)
    {
        LOG_ERR("Sem take failed. Ret: %d", ret);
        return ret;
    }

    if (!sd_init_success)
    {
        k_sem_give(&m_sem_sd_oper_ongoing);
        return -ENODEV;
    }

    if (strlen(filename) > CONFIG_FS_FATFS_MAX_LFN)
    {
        LOG_ERR("Filename is too long");
        k_sem_give(&m_sem_sd_oper_ongoing);
        return -ENAMETOOLONG;
    }

    if ((strlen(abs_path_name) + strlen(filename)) > PATH_MAX_LEN)
    {
        LOG_ERR("Filepath is too long");
        k_sem_give(&m_sem_sd_oper_ongoing);
        return -EINVAL;
    }

    strncat(abs_path_name, filename, avilable_path_space);

    LOG_INF("abs path name:\t%s", abs_path_name);

    fs_file_t_init(f_seg_read_entry);

    ret = fs_open(f_seg_read_entry, abs_path_name, FS_O_READ);
    if (ret)
    {
        LOG_ERR("Open file failed: %d", ret);
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    return 0;
}

int sd_card_read(char *buf, size_t *size, struct fs_file_t *f_seg_read_entry)
{
    int ret;

    if (!(k_sem_count_get(&m_sem_sd_oper_ongoing) <= 0))
    {
        LOG_ERR("SD operation not ongoing");
        return -EPERM;
    }

    ret = fs_read(f_seg_read_entry, buf, *size);
    if (ret < 0)
    {
        LOG_ERR("Read file failed. Ret: %d", ret);
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    *size = ret;

    return 0;
}

int sd_card_close(struct fs_file_t *f_seg_read_entry)
{
    int ret;

    if (k_sem_count_get(&m_sem_sd_oper_ongoing) != 0)
    {
        LOG_ERR("SD operation not ongoing");
        return -EPERM;
    }

    ret = fs_close(f_seg_read_entry);
    if (ret)
    {
        LOG_ERR("Close file failed: %d", ret);
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    k_sem_give(&m_sem_sd_oper_ongoing);
    return 0;
}

int create_directory(const char *path)
{
    int ret;
    struct fs_dir_t dir;

    fs_dir_t_init(&dir);

    ret = fs_mkdir(path);
    if (ret == 0)
    {
        LOG_INF("Directory created successfully: %s", path);
        return 0;
    }
    else if (ret == -EEXIST)
    {
        LOG_INF("Directory already exists: %s", path);
        return -EEXIST; // Return EEXIST to allow the caller to handle it
    }
    else
    {
        LOG_ERR("Failed to create directory %s: error %d", path, ret);
        return ret;
    }
}

int find_highest_session_number(void)
{
    int highest_session = 0;
    struct fs_dir_t dirp;
    struct fs_dirent entry;
    char *prefix = "session_";
    int prefix_len = strlen(prefix);
    int ret;

    // Initialize the directory object
    fs_dir_t_init(&dirp);

    // Open the root directory
    ret = fs_opendir(&dirp, sd_root_path);
    if (ret)
    {
        LOG_ERR("Failed to open root directory %s (err %d)", sd_root_path, ret);
        return -1;
    }

    LOG_INF("Searching for session directories in %s", sd_root_path);

    // Read directory entries
    while (1)
    {
        ret = fs_readdir(&dirp, &entry);
        if (ret)
        {
            LOG_ERR("Failed to read directory entry (err %d)", ret);
            break;
        }

        // Check if we've reached the end of the directory
        if (entry.name[0] == 0)
        {
            break;
        }

        // Check if the entry is a directory and starts with "session_"
        if (entry.type == FS_DIR_ENTRY_DIR &&
            strncmp(entry.name, prefix, prefix_len) == 0)
        {

            // Try to convert the rest of the name to an integer
            int session_num = atoi(entry.name + prefix_len);

            // Update highest_session if this number is greater
            if (session_num > highest_session)
            {
                highest_session = session_num;
            }
        }
    }

    // Close the directory
    fs_closedir(&dirp);

    LOG_INF("Highest session number found: %d", highest_session);

    return highest_session;
}

int sd_card_init(void)
{
    int ret;
    static const char *sd_dev = "SD";
    uint64_t sd_card_size_bytes;
    uint32_t sector_count;
    size_t sector_size;

    // INITIALIZE SD CARD =============================================================================================
    // Check if the SD device is available
    if (!device_is_ready(DEVICE_DT_GET(DT_NODELABEL(spi3))))
    {
        LOG_ERR("SD device is not ready");
        return -ENODEV;
    }
    k_sleep(K_MSEC(1000));

    // Try to initialize disk access =============================================================================================
    ret = disk_access_init(sd_dev);
    if (ret != 0)
    {
        LOG_ERR("disk_access_init failed (err %d)", ret);
        return ret;
    }
    LOG_INF("disk_access_init successful: %d", ret);
    k_sleep(K_MSEC(200));

    // Check disk status =============================================================================================
    ret = disk_access_status(sd_dev);
    if (ret != DISK_STATUS_OK)
    {
        LOG_ERR("disk_access_status failed (status %d)", ret);
        return ret;
    }
    LOG_INF("disk_access_status successful: %d", ret);
    k_sleep(K_MSEC(200));

    // Try to get sector count =============================================================================================
    ret = disk_access_ioctl(sd_dev, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
    if (ret != 0)
    {
        LOG_ERR("Failed to get sector count (err %d)", ret);
        return ret;
    }
    ret = disk_access_ioctl(sd_dev, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
    if (ret != 0)
    {
        LOG_ERR("Failed to get sector size (err %d)", ret);
        return ret;
    }
    LOG_INF("Sector count: %d", sector_count);
    LOG_INF("Sector size: %d bytes", sector_size);
    sd_card_size_bytes = (uint64_t)sector_count * sector_size;
    LOG_INF("SD card volume size: %d MB", (uint32_t)(sd_card_size_bytes >> 20));
    k_sleep(K_MSEC(200));

    // Try to mount the filesystem =============================================================================================
    mnt_pt.mnt_point = sd_root_path; // add the sd_root_path to the mnt_pt struct
    ret = fs_mount(&mnt_pt);
    if (ret != 0)
    {
        LOG_ERR("fs_mount failed (err %d)", ret);
        return ret;
    }
    LOG_INF("SD card initialized and mounted successfully");
    // Add a longer delay after mounting
    k_sleep(K_MSEC(500));

    // Verify the mount point =============================================================================================
    struct fs_statvfs stats;
    ret = fs_statvfs(mnt_pt.mnt_point, &stats);
    if (ret != 0)
    {
        LOG_ERR("Failed to get filesystem stats (err %d)", ret);
        return ret;
    }
    LOG_INF("Filesystem mounted at %s is accessible", mnt_pt.mnt_point);
    k_sleep(K_MSEC(100));

    // LIST FILES AND FIND THE HIGHEST SESSION NUMBER =============================================================
    // char list_buf[4096];
    // size_t buf_size = sizeof(list_buf);
    // ret = sd_card_list_files("/", list_buf, &buf_size);
    // if (ret != 0)
    // {
    //     LOG_ERR("Failed to list files in root directory (err %d)", ret);
    //     return ret;
    // }
    // LOG_INF("Files in root directory:\n%s", list_buf);

    // Find the highest existing session number
    int highest_session = find_highest_session_number();
    if (highest_session < 0)
    {
        LOG_ERR("Failed to determine highest session number");
        highest_session = 0; // Handle error (maybe set a default value)
    }

    // Create a new folder for this session
    uint32_t new_session = highest_session + 1;
    snprintf(current_data_folder, sizeof(current_data_folder),
             "%s/session_%u", sd_root_path, new_session);

    LOG_INF("Attempting to create directory: %s", current_data_folder);
    ret = create_directory(current_data_folder);

    if (ret == 0)
    {
        LOG_INF("Created new data folder: %s", current_data_folder);
    }
    else
    {
        LOG_ERR("Failed to create directory %s, error: %d", current_data_folder, ret);
        return ret;
    }

    sd_init_success = true; // indicate SD card is initialized for other funcs
    return 0;
}

// Static buffers to reduce stack usage
static NeuralData data_buffer[MAX_NEURAL_DATA_PER_WRITE];
static char filename[PATH_MAX_LEN + 1];

void sd_card_writer_thread(void *arg1, void *arg2, void *arg3)
{
    fifo_buffer_t *fifo_buffer = (fifo_buffer_t *)arg1;
    static uint32_t file_counter = 0;
    size_t data_count = 0;

    // Wait for SD card initialization
    while (!sd_init_success)
    {
        k_sleep(K_MSEC(100));
        LOG_INF("Waiting for SD card initialization");
    }

    while (1)
    {
        // Wait for data to be available
        int ret = k_sem_take(&fifo_buffer->data_available, K_MSEC(40));
        if (ret != 0)
        {
            continue;
        }

        LOG_INF("Data sem taken, reading from FIFO buffer");

        // Read data from FIFO buffer
        size_t read_count = read_from_fifo_buffer(fifo_buffer, &data_buffer[data_count], MAX_NEURAL_DATA_PER_WRITE - data_count);
        data_count += read_count;

        LOG_INF("Read %zu NeuralData structs from FIFO buffer now in data_count", read_count);
        LOG_INF("Should we write: %d", (data_count == MAX_NEURAL_DATA_PER_WRITE));

        // Write to SD card if buffer is full or we've read all available data
        if (data_count == MAX_NEURAL_DATA_PER_WRITE || (read_count == 0 && data_count > 0))
        {
            snprintf(filename, PATH_MAX_LEN, "%s/data_%u.bin", current_data_folder, file_counter++);

            size_t bytes_to_write = data_count * sizeof(NeuralData);
            LOG_INF("About to write %zu bytes to file: %s", bytes_to_write, filename);
            int ret = sd_card_open_write_close(filename, data_buffer, &bytes_to_write); // TODO LOOK INTO THIS FUNCTION
            if (ret != 0)
            {
                LOG_ERR("Failed to write to SD card, err: %d", ret);
            }
            else
            {
                LOG_INF("Wrote %zu NeuralData structs (%zu bytes) to %s", data_count, bytes_to_write, filename);
            }

            data_count = 0;
        }

        k_sleep(K_MSEC(50)); // Small delay to prevent tight looping
    }
}

// static char write_buffer[WRITE_BUFFER_SIZE];
// void sd_card_writer_thread(void *arg1, void *arg2, void *arg3)
// {
//     LOG_INF("SD card writer thread started");
//     fifo_buffer_t *fifo_buffer = (fifo_buffer_t *)arg1;
//     static uint32_t file_counter = 0;
//     size_t current_file_size = 0;
//     int64_t last_write_time = k_uptime_get();
//     size_t buffer_offset = 0;

//     // Wait for SD card initialization to complete
//     while (!sd_init_success)
//     {
//         k_sleep(K_MSEC(100));
//     }
//     LOG_INF("SD card initialization complete");

//     // Ensure the current_data_folder is set
//     if (strlen(current_data_folder) == 0)
//     {
//         LOG_ERR("Current data folder not set. Unable to write data.");
//         return;
//     }

//     LOG_INF("SD card writer thread initialized, writing to folder: %s", current_data_folder);

//     while (1)
//     {
//         LOG_INF("Starting new iteration of writer loop");
//         int sem_ret = k_sem_take(&fifo_buffer->data_available, K_NO_WAIT);
//         bool should_write = false;

//         if (sem_ret == 0)
//         {
//             // Read structs read from fifo buffer
//             LOG_INF("Semaphore taken, reading from FIFO buffer");
//             size_t structs_read = read_from_fifo_buffer(fifo_buffer, data, MAX_STRUCTS_PER_WRITE);
//             LOG_INF("Read %zu structs from FIFO buffer", structs_read);

//             if (structs_read > 0)
//             {
//                 if (structs_read > MAX_STRUCTS_PER_WRITE)
//                 {
//                     LOG_ERR("Read more structs than buffer can hold");
//                     structs_read = MAX_STRUCTS_PER_WRITE;
//                 }

//                 // get number of bytes to write
//                 size_t bytes_to_write = structs_read * sizeof(NeuralData);
//                 LOG_INF("Preparing to write %zu bytes", bytes_to_write);

//                 // Check if we need to create a new file, we do if the
//                 if (current_file_size == 0 || current_file_size >= MAX_FILE_SIZE)
//                 {
//                     LOG_INF("Creating new file");
//                     int ret = snprintf(filename, sizeof(filename), "%s/neural_data_%u.bin",
//                                        current_data_folder, file_counter++);
//                     if (ret < 0 || ret >= sizeof(filename))
//                     {
//                         LOG_ERR("Filename truncated or error in snprintf");
//                         k_sleep(K_MSEC(100));
//                         continue; // TODO: come out of loop if filename is broken
//                     }
//                     current_file_size = 0;
//                     LOG_INF("Created new file: %s", filename);
//                 }

//                 // Copy new data to buffer
//                 if (buffer_offset + bytes_to_write > WRITE_BUFFER_SIZE)
//                 {
//                     should_write = true;
//                     LOG_WRN("Write buffer full, forcing write");
//                 }
//                 else
//                 {
//                     if (bytes_to_write <= sizeof(NeuralData) * MAX_STRUCTS_PER_WRITE)
//                     {
//                         LOG_INF("Copying %zu bytes to write buffer", bytes_to_write);
//                         memcpy(write_buffer + buffer_offset, data, bytes_to_write); // copy data to write buffer
//                         buffer_offset += bytes_to_write;
//                         LOG_INF("Copied %zu bytes to write buffer, new offset: %zu", bytes_to_write, buffer_offset);
//                     }
//                     else
//                     {
//                         LOG_ERR("Attempted to write more data than buffer can hold");
//                     }
//                 }
//             }
//             else
//             {
//                 LOG_WRN("Semaphore signaled but no data read from FIFO buffer");
//             }
//         }
//         else
//         {
//             LOG_INF("Semaphore timeout, checking if write is needed");
//         }

//         // Check if it's time to write
//         LOG_INF("Checking write conditions: should_write=%d, time_elapsed=%lld, buffer_offset=%zu",
//                 should_write, (k_uptime_get() - last_write_time), buffer_offset);
//         if (should_write ||
//             (k_uptime_get() - last_write_time) >= WRITE_INTERVAL_MS ||
//             buffer_offset > (WRITE_BUFFER_SIZE / 2))
//         {
//             if (buffer_offset > 0)
//             {
//                 LOG_INF("Attempting to write %zu bytes to %s", buffer_offset, filename);
//                 int ret = sd_card_open_write_close(filename, write_buffer, &buffer_offset);
//                 if (ret != 0)
//                 {
//                     LOG_ERR("Failed to write buffer to SD card, err: %d", ret);
//                 }
//                 else
//                 {
//                     LOG_INF("Successfully wrote %zu bytes to %s", buffer_offset, filename);
//                     current_file_size += buffer_offset;
//                     last_write_time = k_uptime_get();
//                 }
//                 buffer_offset = 0;
//                 LOG_INF("Write complete, buffer_offset reset to 0");
//             }
//             else
//             {
//                 LOG_INF("No data to write, skipping write operation");
//             }
//         }
//         LOG_INF("End of writer loop iteration");
//     }
// }