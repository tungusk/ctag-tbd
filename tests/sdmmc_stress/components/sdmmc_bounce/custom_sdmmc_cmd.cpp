#include <cassert>
#include <cstring>

#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "sdmmc_bounce";

extern "C" esp_err_t sdmmc_read_sectors_dma(
    sdmmc_card_t *card,
    void *dst,
    size_t start_block,
    size_t block_count,
    size_t buffer_len);

extern "C" esp_err_t sdmmc_write_sectors_dma(
    sdmmc_card_t *card,
    const void *src,
    size_t start_block,
    size_t block_count,
    size_t buffer_len);

namespace {

constexpr int MAX_SECTORS_PER_READ = 32;
constexpr int MAX_SECTORS_PER_WRITE = 32;
constexpr size_t DMA_BUFFER_SIZE = MAX_SECTORS_PER_READ * 512;

uint8_t *sector_buffer = nullptr;
size_t sector_buffer_actual_size = 0;

esp_err_t ensure_buffer_allocated() {
    if (sector_buffer != nullptr) {
        return ESP_OK;
    }

    uint32_t caps = MALLOC_CAP_DMA | MALLOC_CAP_32BIT;
#if CONFIG_TEST_SDMMC_BOUNCE_BUFFER_IN_PSRAM
    caps |= MALLOC_CAP_SPIRAM;
#else
    caps |= MALLOC_CAP_INTERNAL;
#endif

    sector_buffer = static_cast<uint8_t *>(heap_caps_malloc(DMA_BUFFER_SIZE, caps));
    if (sector_buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate %zu-byte DMA bounce buffer", DMA_BUFFER_SIZE);
        return ESP_ERR_NO_MEM;
    }

    sector_buffer_actual_size = heap_caps_get_allocated_size(sector_buffer);
    ESP_LOGI(TAG, "SDMMC bounce buffer allocated at %p (read sectors: %d, write sectors: %d, requested: %zu bytes, actual: %zu bytes)",
             sector_buffer, MAX_SECTORS_PER_READ, MAX_SECTORS_PER_WRITE,
             DMA_BUFFER_SIZE, sector_buffer_actual_size);
    return ESP_OK;
}

} // namespace

extern "C" esp_err_t __wrap_sdmmc_read_sectors(sdmmc_card_t *card, void *dst, size_t start_block, size_t block_count) {
    if (block_count == 0) {
        return ESP_OK;
    }

    const size_t block_size = card->csd.sector_size;
    assert(block_size == 512);

    esp_err_t err = ensure_buffer_allocated();
    if (err != ESP_OK) {
        return err;
    }

    auto *cur_dst = static_cast<uint8_t *>(dst);
    size_t blocks_remaining = block_count;
    size_t current_block = start_block;

    while (blocks_remaining > 0) {
        const size_t blocks_to_read = (blocks_remaining > MAX_SECTORS_PER_READ)
            ? MAX_SECTORS_PER_READ
            : blocks_remaining;
        const size_t bytes_to_copy = blocks_to_read * block_size;

        if (bytes_to_copy > sector_buffer_actual_size) {
            ESP_LOGE(TAG, "Buffer too small: need %zu, have %zu", bytes_to_copy, sector_buffer_actual_size);
            return ESP_ERR_NO_MEM;
        }

        err = sdmmc_read_sectors_dma(card, sector_buffer, current_block, blocks_to_read, sector_buffer_actual_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error 0x%x reading %zu blocks at sector %zu", err, blocks_to_read, current_block);
            break;
        }

        memcpy(cur_dst, sector_buffer, bytes_to_copy);
        cur_dst += bytes_to_copy;
        current_block += blocks_to_read;
        blocks_remaining -= blocks_to_read;
    }

    return err;
}

extern "C" esp_err_t __wrap_sdmmc_write_sectors(sdmmc_card_t *card, const void *src, size_t start_block, size_t block_count) {
    if (block_count == 0) {
        return ESP_OK;
    }

    const size_t block_size = card->csd.sector_size;
    assert(block_size == 512);

    esp_err_t err = ensure_buffer_allocated();
    if (err != ESP_OK) {
        return err;
    }

    auto *cur_src = static_cast<const uint8_t *>(src);
    size_t blocks_remaining = block_count;
    size_t current_block = start_block;

    while (blocks_remaining > 0) {
        const size_t blocks_to_write = (blocks_remaining > MAX_SECTORS_PER_WRITE)
            ? MAX_SECTORS_PER_WRITE
            : blocks_remaining;
        const size_t bytes_to_copy = blocks_to_write * block_size;

        if (bytes_to_copy > sector_buffer_actual_size) {
            ESP_LOGE(TAG, "Buffer too small: need %zu, have %zu", bytes_to_copy, sector_buffer_actual_size);
            return ESP_ERR_NO_MEM;
        }

        memcpy(sector_buffer, cur_src, bytes_to_copy);
        err = sdmmc_write_sectors_dma(card, sector_buffer, current_block, blocks_to_write, sector_buffer_actual_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error 0x%x writing %zu blocks at sector %zu", err, blocks_to_write, current_block);
            break;
        }

        cur_src += bytes_to_copy;
        current_block += blocks_to_write;
        blocks_remaining -= blocks_to_write;
    }

    return err;
}

