#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_pwr_ctrl.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "zlib.h"

namespace {

constexpr const char *TAG = "sdmmc_stress";
constexpr const char *MOUNT_POINT = "/sdcard";
constexpr const char *TEST_PATH = "/sdcard/sdmmc_stress.bin";
constexpr const char *ZIP_PATH = "/sdcard/dada-tbd-sd.zip";
constexpr const char *HASH_PATH = "/sdcard/dada-tbd-sd-hash.txt";
constexpr const char *VERSION_PATH = "/sdcard/.version";

sdmmc_card_t *card = nullptr;
sd_pwr_ctrl_handle_t sd_pwr_ctrl_handle = nullptr;

struct PerfStats {
    int count = 0;
    double sum_mib_s = 0.0;
    double min_mib_s = 0.0;
    double max_mib_s = 0.0;

    void add(double value) {
        if (count == 0) {
            min_mib_s = value;
            max_mib_s = value;
        } else {
            min_mib_s = std::min(min_mib_s, value);
            max_mib_s = std::max(max_mib_s, value);
        }
        sum_mib_s += value;
        ++count;
    }

    double avg() const {
        return count > 0 ? sum_mib_s / count : 0.0;
    }
};

struct CyclePerf {
    double write_mib_s = 0.0;
    double read_mib_s = 0.0;
};

esp_err_t sdmmc_host_init_noop() {
    return ESP_OK;
}

bool ensure_sd_power_control() {
    if (sd_pwr_ctrl_handle != nullptr) {
        return true;
    }

    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    const esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &sd_pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create SD on-chip LDO power control driver: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

void sd_power_cycle() {
#if CONFIG_TEST_SDMMC_POWER_CYCLE
    if (CONFIG_TEST_SDMMC_FORCE_3V3_BEFORE_POWER_CYCLE && ensure_sd_power_control()) {
        const esp_err_t ret = sd_pwr_ctrl_set_io_voltage(sd_pwr_ctrl_handle, 3300);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to force SD IO voltage to 3.3V before power cycle: %s", esp_err_to_name(ret));
        }
    }

    const gpio_num_t power_gpio = static_cast<gpio_num_t>(CONFIG_TEST_SDMMC_CARD_POWER_GPIO);
    gpio_config_t io_conf{};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << power_gpio);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level(power_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_TEST_SDMMC_SETTLE_MS));
    gpio_set_level(power_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_TEST_SDMMC_SETTLE_MS));
#endif
}

#if CONFIG_TEST_SDMMC_TIMING_HS_1BIT || CONFIG_TEST_SDMMC_TIMING_HS_4BIT
sdmmc_delay_phase_t configured_hs_delay_phase() {
    switch (CONFIG_TEST_SDMMC_HS_DELAY_PHASE) {
        case 0:
            return SDMMC_DELAY_PHASE_0;
        case 1:
            return SDMMC_DELAY_PHASE_1;
        case 2:
            return SDMMC_DELAY_PHASE_2;
        case 3:
            return SDMMC_DELAY_PHASE_3;
        case 4:
            return SDMMC_DELAY_PHASE_4;
        case 5:
            return SDMMC_DELAY_PHASE_5;
        case 6:
            return SDMMC_DELAY_PHASE_6;
        case 7:
            return SDMMC_DELAY_PHASE_7;
        default:
            return SDMMC_DELAY_PHASE_2;
    }
}
#endif

void configure_host(sdmmc_host_t &host) {
    host = SDMMC_HOST_DEFAULT();
    host.flags |= SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
    host.slot = SDMMC_HOST_SLOT_0;

#if CONFIG_TEST_SDMMC_TIMING_HS_1BIT
    host.flags &= ~SDMMC_HOST_FLAG_DDR;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.input_delay_phase = configured_hs_delay_phase();
#elif CONFIG_TEST_SDMMC_TIMING_HS_4BIT
    host.flags &= ~SDMMC_HOST_FLAG_DDR;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.input_delay_phase = configured_hs_delay_phase();
#elif CONFIG_TEST_SDMMC_TIMING_SDR50_4BIT_PHASE2
    host.flags &= ~SDMMC_HOST_FLAG_DDR;
    host.max_freq_khz = SDMMC_FREQ_SDR50;
    host.input_delay_phase = SDMMC_DELAY_PHASE_2;
#else
#error "No SDMMC timing mode selected"
#endif

#if CONFIG_TEST_ENABLE_ESP_HOSTED && CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
    // Mirror components/drivers/fs.cpp: ESP-Hosted has already initialized the
    // shared SDMMC host before app_main(), so SD mount must only initialize slot 0.
    host.init = &sdmmc_host_init_noop;
#endif
}

void configure_slot(sdmmc_slot_config_t &slot_config) {
    slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.d4 = GPIO_NUM_NC;
    slot_config.d5 = GPIO_NUM_NC;
    slot_config.d6 = GPIO_NUM_NC;
    slot_config.d7 = GPIO_NUM_NC;

#if CONFIG_TEST_SDMMC_TIMING_HS_1BIT
    slot_config.width = 1;
#else
    slot_config.width = 4;
#endif

#if CONFIG_TEST_SDMMC_TIMING_SDR50_4BIT_PHASE2
    slot_config.flags |= SDMMC_SLOT_FLAG_UHS1;
#endif
}

bool mount_sd() {
    sd_power_cycle();
    if (!ensure_sd_power_control()) {
        return false;
    }

    sdmmc_host_t host{};
    configure_host(host);
    host.pwr_ctrl_handle = sd_pwr_ctrl_handle;

    sdmmc_slot_config_t slot_config{};
    configure_slot(slot_config);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 4;
    mount_config.allocation_unit_size = 16 * 1024;

    ESP_LOGI(TAG, "Mounting SD card");
    const esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed: %s", esp_err_to_name(ret));
        card = nullptr;
        return false;
    }

    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG,
             "SD mode: real=%d kHz limit=%" PRIu32 " kHz bus=%" PRIu32 "-bit card_uhs=%d active_uhs=%d ddr=%d ocr=0x%08" PRIx32,
             card->real_freq_khz,
             card->max_freq_khz,
             card->is_mmc ? (1u << card->log_bus_width) : (card->ssr.cur_bus_width ? 4u : 1u),
             static_cast<int>(card->is_uhs1),
             static_cast<int>(card->real_freq_khz > SDMMC_FREQ_HIGHSPEED),
             static_cast<int>(card->is_ddr),
             card->ocr);
    return true;
}

bool sd_card_is_active_uhs() {
    return card != nullptr && card->real_freq_khz > SDMMC_FREQ_HIGHSPEED;
}

void unmount_sd();

bool mount_sd_with_uhs_retry() {
#if CONFIG_TEST_SDMMC_TIMING_SDR50_4BIT_PHASE2
    const int max_attempts = 1 + CONFIG_TEST_SDMMC_RETRY_NON_UHS_MOUNTS;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        if (!mount_sd()) {
            return false;
        }

        if (sd_card_is_active_uhs()) {
            if (attempt > 1) {
                ESP_LOGI(TAG, "SD UHS recovered after mount attempt %d/%d", attempt, max_attempts);
            }
            return true;
        }

        ESP_LOGW(TAG,
                 "SD mounted below UHS speed on attempt %d/%d; real=%d kHz limit=%" PRIu32 " kHz card_uhs=%d",
                 attempt,
                 max_attempts,
                 card->real_freq_khz,
                 card->max_freq_khz,
                 static_cast<int>(card->is_uhs1));
        unmount_sd();
        if (attempt < max_attempts) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ESP_LOGE(TAG, "SD failed to enter UHS mode after %d mount attempts", max_attempts);
    return false;
#else
    return mount_sd();
#endif
}

void unmount_sd() {
    if (card != nullptr) {
        ESP_LOGI(TAG, "Unmounting SD card");
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
        card = nullptr;
    }
}

bool read_text_file(const char *path, std::string &out) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    char buffer[128] = {};
    const size_t len = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    out.assign(buffer, len);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return !out.empty();
}

bool write_text_file(const char *path, const std::string &value) {
    FILE *file = fopen(path, "wb");
    if (file == nullptr) {
        ESP_LOGW(TAG, "Failed to create %s", path);
        return false;
    }
    const bool ok = fwrite(value.data(), 1, value.size(), file) == value.size();
    fclose(file);
    return ok;
}

bool write_full(int fd, const uint8_t *buffer, size_t len);

void delete_dir_recursive(const std::string &path) {
    DIR *dir = opendir(path.c_str());
    if (dir == nullptr) {
        return;
    }

    dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string child = path + "/" + entry->d_name;
        struct stat st {};
        if (stat(child.c_str(), &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            delete_dir_recursive(child);
        } else {
            unlink(child.c_str());
        }
    }
    closedir(dir);
    rmdir(path.c_str());
}

uint16_t read_le16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_le32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void make_parent_dirs(const std::string &full_path) {
    size_t slash = full_path.find_last_of('/');
    if (slash == std::string::npos) {
        return;
    }

    const std::string dir_path = full_path.substr(0, slash);
    size_t start = strlen(MOUNT_POINT) + 1;
    while ((slash = dir_path.find('/', start)) != std::string::npos) {
        mkdir(dir_path.substr(0, slash).c_str(), 0755);
        start = slash + 1;
    }
    mkdir(dir_path.c_str(), 0755);
}

bool extract_zip_to_sd(const char *zip_path, const char *dest_dir) {
    ESP_LOGI(TAG, "Extracting %s to %s", zip_path, dest_dir);

    FILE *zip_file = fopen(zip_path, "rb");
    if (zip_file == nullptr) {
        ESP_LOGE(TAG, "Failed to open ZIP file: %s", zip_path);
        return false;
    }

    fseek(zip_file, 0, SEEK_END);
    const long file_size = ftell(zip_file);
    const long search_start = (file_size > 65536) ? (file_size - 65536) : 0;
    const size_t search_size = static_cast<size_t>(file_size - search_start);
    fseek(zip_file, search_start, SEEK_SET);

    uint8_t *search_buf = static_cast<uint8_t *>(heap_caps_malloc(search_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (search_buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate ZIP EOCD search buffer");
        fclose(zip_file);
        return false;
    }
    fread(search_buf, 1, search_size, zip_file);

    long eocd_offset = -1;
    for (long i = static_cast<long>(search_size) - 22; i >= 0; --i) {
        if (search_buf[i] == 0x50 && search_buf[i + 1] == 0x4b &&
            search_buf[i + 2] == 0x05 && search_buf[i + 3] == 0x06) {
            eocd_offset = search_start + i;
            break;
        }
    }
    heap_caps_free(search_buf);

    if (eocd_offset < 0) {
        ESP_LOGE(TAG, "No ZIP EOCD found");
        fclose(zip_file);
        return false;
    }

    fseek(zip_file, eocd_offset + 10, SEEK_SET);
    uint8_t eocd_fields[10] = {};
    fread(eocd_fields, 1, sizeof(eocd_fields), zip_file);
    const uint16_t num_entries = read_le16(eocd_fields);
    const uint32_t cd_size = read_le32(eocd_fields + 2);
    const uint32_t cd_offset = read_le32(eocd_fields + 6);

    ESP_LOGI(TAG, "ZIP: %u entries, central dir size=%" PRIu32 " offset=%" PRIu32,
             num_entries, cd_size, cd_offset);

    fseek(zip_file, cd_offset, SEEK_SET);
    uint8_t *cd_buf = static_cast<uint8_t *>(heap_caps_malloc(cd_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (cd_buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate ZIP central directory buffer");
        fclose(zip_file);
        return false;
    }
    fread(cd_buf, 1, cd_size, zip_file);

    constexpr size_t chunk_size = 64 * 1024;
    uint8_t *in_buf = static_cast<uint8_t *>(heap_caps_malloc(chunk_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_32BIT));
    uint8_t *out_buf = static_cast<uint8_t *>(heap_caps_malloc(chunk_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_32BIT));
    if (in_buf == nullptr || out_buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate ZIP IO buffers");
        heap_caps_free(cd_buf);
        heap_caps_free(in_buf);
        heap_caps_free(out_buf);
        fclose(zip_file);
        return false;
    }

    uint32_t cd_pos = 0;
    int files_processed = 0;
    int files_extracted = 0;
    int dirs_processed = 0;
    bool all_entries_ok = true;
    uint32_t last_log_time = esp_log_timestamp();

    const int64_t start_us = esp_timer_get_time();
    while (cd_pos + 46 <= cd_size) {
        if (cd_buf[cd_pos] != 0x50 || cd_buf[cd_pos + 1] != 0x4b ||
            cd_buf[cd_pos + 2] != 0x01 || cd_buf[cd_pos + 3] != 0x02) {
            break;
        }

        const uint16_t comp_method = read_le16(cd_buf + cd_pos + 10);
        const uint32_t comp_size = read_le32(cd_buf + cd_pos + 20);
        const uint32_t uncomp_size = read_le32(cd_buf + cd_pos + 24);
        const uint16_t name_len = read_le16(cd_buf + cd_pos + 28);
        const uint16_t extra_len = read_le16(cd_buf + cd_pos + 30);
        const uint16_t comment_len = read_le16(cd_buf + cd_pos + 32);
        const uint32_t local_header_offset = read_le32(cd_buf + cd_pos + 42);

        std::string filename(reinterpret_cast<char *>(cd_buf + cd_pos + 46), name_len);
        std::string full_path = std::string(dest_dir) + "/" + filename;
        ++files_processed;

        const uint32_t now = esp_log_timestamp();
        if ((files_processed % 10 == 0) || (now - last_log_time > 2000)) {
            ESP_LOGI(TAG, "ZIP progress: %d/%u files (%.1f%%) - %s",
                     files_processed, num_entries,
                     (files_processed * 100.0f) / num_entries,
                     filename.c_str());
            last_log_time = now;
        }

        if (!filename.empty() && filename.back() == '/') {
            mkdir(full_path.c_str(), 0755);
            ++dirs_processed;
            cd_pos += 46 + name_len + extra_len + comment_len;
            continue;
        }

        make_parent_dirs(full_path);

        fseek(zip_file, local_header_offset, SEEK_SET);
        uint8_t local_header[30] = {};
        fread(local_header, 1, sizeof(local_header), zip_file);
        const uint16_t local_name_len = read_le16(local_header + 26);
        const uint16_t local_extra_len = read_le16(local_header + 28);
        const uint32_t data_offset = local_header_offset + 30 + local_name_len + local_extra_len;
        fseek(zip_file, data_offset, SEEK_SET);

        int out_fd = open(full_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            ESP_LOGE(TAG, "Failed to create %s: errno=%d (%s)", full_path.c_str(), errno, strerror(errno));
            all_entries_ok = false;
            cd_pos += 46 + name_len + extra_len + comment_len;
            continue;
        }

        if (uncomp_size > 0) {
            lseek(out_fd, uncomp_size - 1, SEEK_SET);
            const uint8_t zero = 0;
            write(out_fd, &zero, 1);
            lseek(out_fd, 0, SEEK_SET);
        }

        bool file_ok = true;
        if (comp_method == 0) {
            uint32_t remaining = uncomp_size;
            while (remaining > 0) {
                const uint32_t to_read = std::min<uint32_t>(remaining, chunk_size);
                const size_t bytes_read = fread(in_buf, 1, to_read, zip_file);
                if (bytes_read == 0 || !write_full(out_fd, in_buf, bytes_read)) {
                    ESP_LOGE(TAG, "ZIP stored write/read error for %s", full_path.c_str());
                    file_ok = false;
                    break;
                }
                remaining -= static_cast<uint32_t>(bytes_read);
            }
        } else if (comp_method == 8) {
            z_stream stream {};
            int ret = inflateInit2(&stream, -MAX_WBITS);
            if (ret != Z_OK) {
                ESP_LOGE(TAG, "inflateInit2 failed: %d", ret);
                file_ok = false;
            }

            uint32_t total_in = 0;
            while (file_ok && ret != Z_STREAM_END && total_in < comp_size) {
                const uint32_t to_read = std::min<uint32_t>(comp_size - total_in, chunk_size);
                stream.avail_in = fread(in_buf, 1, to_read, zip_file);
                total_in += stream.avail_in;
                if (stream.avail_in == 0) {
                    break;
                }
                stream.next_in = in_buf;

                do {
                    stream.avail_out = chunk_size;
                    stream.next_out = out_buf;
                    ret = inflate(&stream, Z_NO_FLUSH);
                    if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                        ESP_LOGE(TAG, "inflate error %d for %s", ret, full_path.c_str());
                        file_ok = false;
                        break;
                    }
                    const size_t have = chunk_size - stream.avail_out;
                    if (have > 0 && !write_full(out_fd, out_buf, have)) {
                        ESP_LOGE(TAG, "ZIP deflate write error for %s", full_path.c_str());
                        file_ok = false;
                        break;
                    }
                } while (stream.avail_out == 0);
            }
            inflateEnd(&stream);
        } else {
            ESP_LOGW(TAG, "Unsupported ZIP compression method %u for %s", comp_method, filename.c_str());
            file_ok = false;
        }

        if (fsync(out_fd) != 0) {
            ESP_LOGE(TAG, "fsync failed for %s: errno=%d (%s)", full_path.c_str(), errno, strerror(errno));
            file_ok = false;
        }
        close(out_fd);
        if (file_ok) {
            ++files_extracted;
        } else {
            all_entries_ok = false;
        }

        cd_pos += 46 + name_len + extra_len + comment_len;
    }

    const int64_t end_us = esp_timer_get_time();
    heap_caps_free(cd_buf);
    heap_caps_free(in_buf);
    heap_caps_free(out_buf);
    fclose(zip_file);

    ESP_LOGI(TAG, "ZIP extraction complete: %d files, %d dirs, %d/%u entries in %.2f s",
             files_extracted,
             dirs_processed,
             files_processed,
             num_entries,
             static_cast<double>(end_us - start_us) / 1000000.0);
    return all_entries_ok && files_processed == num_entries;
}

bool run_boot_zip_extract() {
#if CONFIG_TEST_SDMMC_EXTRACT_ZIP_AT_BOOT
    struct stat st {};
    if (stat(ZIP_PATH, &st) != 0) {
        ESP_LOGE(TAG, "Boot ZIP not found: %s", ZIP_PATH);
        return false;
    }

    ESP_LOGI(TAG, "Deleting existing SD content directories before ZIP extraction");
    delete_dir_recursive("/sdcard/data");
    delete_dir_recursive("/sdcard/www");
    delete_dir_recursive("/sdcard/samples");
    unlink(VERSION_PATH);

    const bool ok = extract_zip_to_sd(ZIP_PATH, MOUNT_POINT);
    if (!ok) {
        ESP_LOGE(TAG, "Boot ZIP extraction failed");
        return false;
    }

    std::string hash;
    if (read_text_file(HASH_PATH, hash)) {
        write_text_file(VERSION_PATH, hash);
        ESP_LOGI(TAG, "Wrote %s from %s", VERSION_PATH, HASH_PATH);
    } else {
        ESP_LOGW(TAG, "No hash file found, leaving %s absent", VERSION_PATH);
    }
#endif
    return true;
}

bool run_sequential_read_benchmark(const char *path, uint8_t *buffer, size_t chunk_size, double *mib_s) {
#if CONFIG_TEST_SDMMC_READ_ZIP_BENCHMARK
    struct stat st {};
    if (stat(path, &st) != 0) {
        ESP_LOGW(TAG, "Read benchmark skipped, file not found: %s", path);
        return true;
    }

    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Read benchmark open failed for %s: errno=%d (%s)", path, errno, strerror(errno));
        return false;
    }

    const int64_t start_us = esp_timer_get_time();
    size_t total = 0;
    while (true) {
        const ssize_t rd = read(fd, buffer, chunk_size);
        if (rd < 0) {
            ESP_LOGE(TAG, "Read benchmark failed for %s: errno=%d (%s)", path, errno, strerror(errno));
            close(fd);
            return false;
        }
        if (rd == 0) {
            break;
        }
        total += static_cast<size_t>(rd);
    }
    close(fd);

    const int64_t end_us = esp_timer_get_time();
    const double seconds = static_cast<double>(end_us - start_us) / 1000000.0;
    const double mib = static_cast<double>(total) / (1024.0 * 1024.0);
    const double speed = seconds > 0.0 ? mib / seconds : 0.0;
    if (mib_s != nullptr) {
        *mib_s = speed;
    }
    ESP_LOGI(TAG, "ZIP read benchmark: %.2f MiB in %.2f s = %.2f MiB/s",
             mib, seconds, speed);
#else
    (void)path;
    (void)buffer;
    (void)chunk_size;
    if (mib_s != nullptr) {
        *mib_s = 0.0;
    }
#endif
    return true;
}

uint32_t pattern_word(size_t offset, int cycle) {
    uint32_t x = static_cast<uint32_t>(offset) ^ (static_cast<uint32_t>(cycle) * 0x9e3779b9u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

void fill_pattern(uint8_t *buffer, size_t len, size_t absolute_offset, int cycle) {
    size_t pos = 0;
    while (pos < len) {
        const uint32_t word = pattern_word(absolute_offset + pos, cycle);
        const size_t todo = std::min(sizeof(word), len - pos);
        memcpy(buffer + pos, &word, todo);
        pos += todo;
    }
}

bool verify_pattern(const uint8_t *buffer, size_t len, size_t absolute_offset, int cycle) {
    uint8_t expected[sizeof(uint32_t)];
    size_t pos = 0;
    while (pos < len) {
        const uint32_t word = pattern_word(absolute_offset + pos, cycle);
        const size_t todo = std::min(sizeof(word), len - pos);
        memcpy(expected, &word, todo);
        if (memcmp(buffer + pos, expected, todo) != 0) {
            ESP_LOGE(TAG, "Verify mismatch at file offset %zu", absolute_offset + pos);
            return false;
        }
        pos += todo;
    }
    return true;
}

bool write_full(int fd, const uint8_t *buffer, size_t len) {
    size_t done = 0;
    while (done < len) {
        const ssize_t written = write(fd, buffer + done, len - done);
        if (written < 0) {
            ESP_LOGE(TAG, "write failed: errno=%d (%s)", errno, strerror(errno));
            return false;
        }
        done += static_cast<size_t>(written);
    }
    return true;
}

bool read_full(int fd, uint8_t *buffer, size_t len) {
    size_t done = 0;
    while (done < len) {
        const ssize_t rd = read(fd, buffer + done, len - done);
        if (rd < 0) {
            ESP_LOGE(TAG, "read failed: errno=%d (%s)", errno, strerror(errno));
            return false;
        }
        if (rd == 0) {
            ESP_LOGE(TAG, "short read at %zu/%zu bytes", done, len);
            return false;
        }
        done += static_cast<size_t>(rd);
    }
    return true;
}

bool run_file_cycle(int cycle, uint8_t *buffer, size_t chunk_size, size_t file_size, CyclePerf *perf) {
    const int64_t write_start = esp_timer_get_time();
    int fd = open(TEST_PATH, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        ESP_LOGE(TAG, "open write failed: errno=%d (%s)", errno, strerror(errno));
        return false;
    }

    for (size_t offset = 0; offset < file_size; offset += chunk_size) {
        const size_t todo = std::min(chunk_size, file_size - offset);
        fill_pattern(buffer, todo, offset, cycle);
        if (!write_full(fd, buffer, todo)) {
            close(fd);
            return false;
        }
#if CONFIG_TEST_SDMMC_FSYNC_EACH_CHUNK
        if (fsync(fd) != 0) {
            ESP_LOGE(TAG, "fsync chunk failed: errno=%d (%s)", errno, strerror(errno));
            close(fd);
            return false;
        }
#endif
    }

    if (fsync(fd) != 0) {
        ESP_LOGE(TAG, "fsync final failed: errno=%d (%s)", errno, strerror(errno));
        close(fd);
        return false;
    }
    close(fd);
    const int64_t write_end = esp_timer_get_time();

    const int64_t read_start = esp_timer_get_time();
    fd = open(TEST_PATH, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "open read failed: errno=%d (%s)", errno, strerror(errno));
        return false;
    }

    for (size_t offset = 0; offset < file_size; offset += chunk_size) {
        const size_t todo = std::min(chunk_size, file_size - offset);
        if (!read_full(fd, buffer, todo)) {
            close(fd);
            return false;
        }
        if (!verify_pattern(buffer, todo, offset, cycle)) {
            close(fd);
            return false;
        }
    }
    close(fd);
    const int64_t read_end = esp_timer_get_time();

    const double mib = static_cast<double>(file_size) / (1024.0 * 1024.0);
    const double write_s = static_cast<double>(write_end - write_start) / 1000000.0;
    const double read_s = static_cast<double>(read_end - read_start) / 1000000.0;
    const double write_mib_s = write_s > 0.0 ? mib / write_s : 0.0;
    const double read_mib_s = read_s > 0.0 ? mib / read_s : 0.0;
    if (perf != nullptr) {
        perf->write_mib_s = write_mib_s;
        perf->read_mib_s = read_mib_s;
    }
    ESP_LOGI(TAG, "cycle %d OK: write %.2f MiB in %.2f s = %.2f MiB/s, read+verify %.2f MiB in %.2f s = %.2f MiB/s",
             cycle, mib, write_s, write_mib_s, mib, read_s, read_mib_s);
    return true;
}

} // namespace

extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting SDMMC stress test");
    ESP_LOGI(TAG, "cycles=%d file=%d KiB chunk=%d KiB remount=%d bounce=%d hosted=%d",
             CONFIG_TEST_SDMMC_CYCLES,
             CONFIG_TEST_SDMMC_FILE_SIZE_KB,
             CONFIG_TEST_SDMMC_IO_CHUNK_KB,
             CONFIG_TEST_SDMMC_UNMOUNT_EACH_CYCLE,
             CONFIG_TEST_SDMMC_USE_BOUNCE_WRAPPER,
             static_cast<int>(
#ifdef CONFIG_TEST_ENABLE_ESP_HOSTED
                 true
#else
                 false
#endif
             ));

    const size_t chunk_size = static_cast<size_t>(CONFIG_TEST_SDMMC_IO_CHUNK_KB) * 1024;
    const size_t file_size = static_cast<size_t>(CONFIG_TEST_SDMMC_FILE_SIZE_KB) * 1024;
    uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(chunk_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate %zu-byte test buffer", chunk_size);
        return;
    }

    PerfStats zip_read_stats;
    PerfStats write_stats;
    PerfStats read_stats;
    int successful_cycles = 0;

    for (int cycle = 0; cycle < CONFIG_TEST_SDMMC_CYCLES; ++cycle) {
        if (card == nullptr && !mount_sd_with_uhs_retry()) {
            break;
        }

        if (cycle == 0) {
            double zip_read_mib_s = 0.0;
            if (!run_sequential_read_benchmark(ZIP_PATH, buffer, chunk_size, &zip_read_mib_s)) {
                ESP_LOGE(TAG, "Stopping after ZIP read benchmark failure");
                break;
            }
#if CONFIG_TEST_SDMMC_READ_ZIP_BENCHMARK
            if (zip_read_mib_s > 0.0) {
                zip_read_stats.add(zip_read_mib_s);
            }
#endif
        }

        if (cycle == 0 && !run_boot_zip_extract()) {
            ESP_LOGE(TAG, "Stopping after boot ZIP extraction failure");
            break;
        }

        CyclePerf perf {};
        if (!run_file_cycle(cycle, buffer, chunk_size, file_size, &perf)) {
            ESP_LOGE(TAG, "Stopping after failed cycle %d", cycle);
            break;
        }
        write_stats.add(perf.write_mib_s);
        read_stats.add(perf.read_mib_s);
        ++successful_cycles;

#if CONFIG_TEST_SDMMC_UNMOUNT_EACH_CYCLE
        unmount_sd();
#endif

        if (CONFIG_TEST_SDMMC_REBOOT_AFTER_CYCLES > 0 &&
            successful_cycles >= CONFIG_TEST_SDMMC_REBOOT_AFTER_CYCLES) {
            ESP_LOGW(TAG,
                     "Rebooting after %d successful cycles as configured",
                     successful_cycles);
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (zip_read_stats.count > 0) {
        ESP_LOGI(TAG, "ZIP read summary: avg %.2f MiB/s min %.2f max %.2f samples=%d",
                 zip_read_stats.avg(),
                 zip_read_stats.min_mib_s,
                 zip_read_stats.max_mib_s,
                 zip_read_stats.count);
    }
    if (successful_cycles > 0) {
        ESP_LOGI(TAG, "IO summary: cycles=%d write avg %.2f MiB/s min %.2f max %.2f; read+verify avg %.2f MiB/s min %.2f max %.2f",
                 successful_cycles,
                 write_stats.avg(),
                 write_stats.min_mib_s,
                 write_stats.max_mib_s,
                 read_stats.avg(),
                 read_stats.min_mib_s,
                 read_stats.max_mib_s);
    }

    unmount_sd();
    heap_caps_free(buffer);
    ESP_LOGI(TAG, "SDMMC stress test finished");
}
