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
/* Maximum length for path support by Windows file system */
#define PATH_MAX_LEN 260
#define K_SEM_OPER_TIMEOUT_MS 1000

K_SEM_DEFINE(m_sem_sd_oper_ongoing, 1, 1);

static const char *sd_root_path = "/SD:";
static FATFS fat_fs;
static bool sd_init_success;

static struct fs_mount_t mnt_pt = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
};

static char current_data_folder[PATH_MAX_LEN + 1];

#define MAX_STRUCTS_PER_WRITE 30
#define WRITE_INTERVAL_MS 1
#define MAX_FILE_SIZE (1024 * 1024) // 1 MB

int sd_card_list_files(char const *const path, char *buf, size_t *buf_size)
{
    int ret;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;
    char abs_path_name[PATH_MAX_LEN + 1] = SD_ROOT_PATH;
    size_t used_buf_size = 0;

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
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    *buf_size = used_buf_size;
    k_sem_give(&m_sem_sd_oper_ongoing);
    return 0;
}

int sd_card_open_write_close(char const *const filename, char const *const data, size_t *size)
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
        return -ENAMETOOLONG;
    }

    strcat(abs_path_name, filename);
    fs_file_t_init(&f_entry);

    ret = fs_open(&f_entry, abs_path_name, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
    if (ret)
    {
        LOG_ERR("Create file failed");
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    /* If the file exists, moves the file position pointer to the end of the file */
    ret = fs_seek(&f_entry, 0, FS_SEEK_END);
    if (ret)
    {
        LOG_ERR("Seek file pointer failed");
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    ret = fs_write(&f_entry, data, *size);
    if (ret < 0)
    {
        LOG_ERR("Write file failed");
        k_sem_give(&m_sem_sd_oper_ongoing);
        return ret;
    }

    *size = ret;

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
    if (ret && ret != -EEXIST)
    {
        LOG_ERR("Failed to create directory %s: %d", path, ret);
        return ret;
    }

    return 0;
}

int sd_card_init(void)
{
    int ret;
    static const char *sd_dev = "SD";
    uint64_t sd_card_size_bytes;
    uint32_t sector_count;
    size_t sector_size;

    LOG_INF("Initializing SD card...");

    // Check if the SD device is available
    if (!device_is_ready(DEVICE_DT_GET(DT_NODELABEL(spi3))))
    {
        LOG_ERR("SD device is not ready");
        return -ENODEV;
    }

    // Try to initialize disk access
    LOG_DBG("Calling disk_access_init...");
    ret = disk_access_init(sd_dev);
    if (ret != 0)
    {
        LOG_ERR("disk_access_init failed (err %d)", ret);
        return ret;
    }

    // Check disk status
    LOG_DBG("Checking disk status...");
    ret = disk_access_status(sd_dev);
    if (ret != DISK_STATUS_OK)
    {
        LOG_ERR("disk_access_status failed (status %d)", ret);
        return ret;
    }

    // Try to get sector count
    LOG_DBG("Getting sector count...");
    ret = disk_access_ioctl(sd_dev, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
    if (ret != 0)
    {
        LOG_ERR("Failed to get sector count (err %d)", ret);
        return ret;
    }
    LOG_DBG("Sector count: %d", sector_count);

    // Try to get sector size
    LOG_DBG("Getting sector size...");
    ret = disk_access_ioctl(sd_dev, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
    if (ret != 0)
    {
        LOG_ERR("Failed to get sector size (err %d)", ret);
        return ret;
    }
    LOG_DBG("Sector size: %d bytes", sector_size);

    sd_card_size_bytes = (uint64_t)sector_count * sector_size;
    LOG_INF("SD card volume size: %d MB", (uint32_t)(sd_card_size_bytes >> 20));

    // Try to mount the filesystem
    LOG_DBG("Mounting filesystem...");
    mnt_pt.mnt_point = sd_root_path; // add the sd_root_path to the mnt_pt struct
    ret = fs_mount(&mnt_pt);
    if (ret != 0)
    {
        LOG_ERR("fs_mount failed (err %d)", ret);
        return ret;
    }

    LOG_INF("SD card initialized and mounted successfully");

    // Create a new folder for this session
    uint32_t folder_counter = 0;
    do
    {
        snprintf(current_data_folder, sizeof(current_data_folder),
                 "%s/session_%u", sd_root_path, folder_counter++);
    } while (create_directory(current_data_folder) == -EEXIST && folder_counter < UINT32_MAX);

    if (folder_counter == UINT32_MAX)
    {
        LOG_ERR("Failed to create a unique session folder");
        return -ENOSPC;
    }

    LOG_INF("Created new data folder: %s", current_data_folder);

    sd_init_success = true;
    return 0;
}

void sd_card_writer_thread(void *arg1, void *arg2, void *arg3)
{
    fifo_buffer_t *fifo_buffer = (fifo_buffer_t *)arg1;
    NeuralData data[MAX_STRUCTS_PER_WRITE];
    char filename[32];
    static uint32_t file_counter = 0;
    size_t current_file_size = 0;
    int64_t last_write_time = k_uptime_get();

    while (1)
    {
        size_t structs_read = read_from_fifo_buffer(fifo_buffer, data, MAX_STRUCTS_PER_WRITE);

        if (structs_read > 0 || (k_uptime_get() - last_write_time) >= WRITE_INTERVAL_MS)
        {
            if (current_file_size == 0 || current_file_size >= MAX_FILE_SIZE)
            {
                snprintf(filename, sizeof(filename), "%s/neural_data_%u.bin",
                         current_data_folder, file_counter++);
                current_file_size = 0;
            }

            size_t bytes_to_write = structs_read * sizeof(NeuralData);
            int ret = sd_card_open_write_close(filename, (const char *)data, &bytes_to_write);
            if (ret != 0)
            {
                LOG_ERR("Failed to write data to SD card, err: %d", ret);
            }
            else
            {
                LOG_INF("Wrote %zu bytes to %s", bytes_to_write, filename);
                current_file_size += bytes_to_write;
                last_write_time = k_uptime_get();
            }
        }

        // Sleep for a short time to allow other threads to run
        k_sleep(K_MSEC(10));
    }
}