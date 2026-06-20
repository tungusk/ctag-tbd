/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

(c) 2026 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt
***************/

#include "codec_ak4619.hpp"

#include <algorithm>
#include <cmath>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/i2s_tdm.h>
#include <esp_attr.h>
#include <esp_check.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace CTAG::DRIVERS;

static const char *TAG = "AK4619";

static i2c_master_bus_handle_t i2c_bus = nullptr;
static i2c_master_dev_handle_t i2c_dev = nullptr;
static i2s_chan_handle_t tx_handle = nullptr;
static i2s_chan_handle_t rx_handle = nullptr;

#define AK4619_ADDR 0x10

#define I2C_PORT_NUM I2C_NUM_1
#define I2C_SDA GPIO_NUM_7
#define I2C_SCL GPIO_NUM_8
#define I2C_CLK_SPEED 400000

#define I2S_PORT_NUM I2S_NUM_0
#define I2S_MCLK GPIO_NUM_13
#define I2S_BCLK GPIO_NUM_12
#define I2S_WS GPIO_NUM_10
#define I2S_DOUT GPIO_NUM_11
#define I2S_DIN GPIO_NUM_9

#define AK4619_RESET_PIN GPIO_NUM_32

#define REG_POWER         0x00
#define REG_AUDIO_IF0     0x01
#define REG_AUDIO_IF1     0x02
#define REG_SYSCLK        0x03
#define REG_ADC_VOL0      0x06
#define REG_ADC_VOL1      0x07
#define REG_ADC_VOL2      0x08
#define REG_ADC_VOL3      0x09
#define REG_ADC_IN_SEL    0x0B
#define REG_ADC_MUTE_HPF  0x0D
#define REG_DAC_VOL0      0x0E
#define REG_DAC_VOL1      0x0F
#define REG_DAC_VOL2      0x10
#define REG_DAC_VOL3      0x11

static bool initialized = false;
static constexpr uint32_t MAX_CODEC_FRAMES = 32;
static constexpr uint8_t AK4619_DAC_0DB = 0x18;
static constexpr uint8_t AK4619_ADC_0DB = 0x30;
static int32_t DRAM_ATTR i2s_tmp[MAX_CODEC_FRAMES * 4] __attribute__((aligned(4)));

static void cleanup() {
    if (tx_handle != nullptr) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = nullptr;
    }
    if (rx_handle != nullptr) {
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
        rx_handle = nullptr;
    }
    if (i2c_dev != nullptr) {
        i2c_master_bus_rm_device(i2c_dev);
        i2c_dev = nullptr;
    }
    if (i2c_bus != nullptr) {
        i2c_del_master_bus(i2c_bus);
        i2c_bus = nullptr;
    }
    initialized = false;
}

static esp_err_t write_reg(uint8_t reg, uint8_t val) {
    uint8_t data[2] = {reg, val};
    return i2c_master_transmit(i2c_dev, data, sizeof(data), 100);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *val) {
    return i2c_master_transmit_receive(i2c_dev, &reg, 1, val, 1, 100);
}

static uint8_t output_level_to_ak_volume(uint32_t level) {
    // The legacy AIC driver uses 58 as the boot-time 0 dB level. Preserve that
    // semantic so AK4619 comes up at unity gain / nominal 1 Vrms output.
    if (level >= 58) return AK4619_DAC_0DB;

    const float db = -63.0f + (static_cast<float>(level) / 58.0f) * 63.0f;
    int reg = static_cast<int>((12.0f - db) / 0.5f + 0.5f);
    if (reg < AK4619_DAC_0DB) reg = AK4619_DAC_0DB;
    if (reg > 0xFE) reg = 0xFE;
    return static_cast<uint8_t>(reg);
}

static esp_err_t setup_reset_pin() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1ULL << AK4619_RESET_PIN;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "reset pin config failed");
    gpio_set_level(AK4619_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(AK4619_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static esp_err_t setup_i2c() {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = false,
            .allow_pd = false,
        },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &i2c_bus), TAG, "I2C bus create failed");

    if (i2c_master_probe(i2c_bus, AK4619_ADDR, 100) != ESP_OK) {
        ESP_LOGI(TAG, "AK4619 not present at I2C address 0x%02X", AK4619_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AK4619_ADDR,
        .scl_speed_hz = I2C_CLK_SPEED,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = false,
        },
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &i2c_dev), TAG, "I2C add device failed");
    return ESP_OK;
}

static esp_err_t setup_i2s() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 32;

    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle), TAG, "I2S channel create failed");

    i2s_tdm_clk_config_t clk_cfg = {
        .sample_rate_hz = 44100,
        .clk_src = I2S_CLK_SRC_APLL,
        .ext_clk_freq_hz = 0,
        .mclk_multiple = I2S_MCLK_MULTIPLE_384,
        .bclk_div = 0,
    };

    i2s_tdm_slot_config_t slot_cfg =
        I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                            I2S_SLOT_MODE_STEREO,
                                            static_cast<i2s_tdm_slot_mask_t>(
                                                I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                                                I2S_TDM_SLOT2 | I2S_TDM_SLOT3));

    i2s_tdm_gpio_config_t gpio_cfg = {
        .mclk = I2S_MCLK,
        .bclk = I2S_BCLK,
        .ws = I2S_WS,
        .dout = I2S_DOUT,
        .din = I2S_DIN,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    };

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg,
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(tx_handle, &tdm_cfg), TAG, "I2S TX TDM init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(rx_handle, &tdm_cfg), TAG, "I2S RX TDM init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_handle), TAG, "I2S TX enable failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_handle), TAG, "I2S RX enable failed");
    return ESP_OK;
}

static esp_err_t configure_codec_standby() {
    uint8_t val = 0;
    ESP_RETURN_ON_ERROR(read_reg(REG_POWER, &val), TAG, "read power failed");
    ESP_LOGI(TAG, "Power register after reset: 0x%02X", val);

    ESP_RETURN_ON_ERROR(write_reg(REG_AUDIO_IF0, 0xAC), TAG, "write AUDIO_IF0 failed");
    // SLOT=1, 32-bit SDIN, 24-bit SDOUT. AK4619 does not support 32-bit SDOUT
    // (DODL=11 is N/A), so ADC data is emitted as 24-bit audio in each 32-bit slot.
    ESP_RETURN_ON_ERROR(write_reg(REG_AUDIO_IF1, 0x1C), TAG, "write AUDIO_IF1 failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_SYSCLK, 0x02), TAG, "write SYSCLK failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_ADC_IN_SEL, 0x55), TAG, "write ADC input select failed");

    ESP_RETURN_ON_ERROR(write_reg(REG_ADC_VOL0, AK4619_ADC_0DB), TAG, "write ADC vol0 failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_ADC_VOL1, AK4619_ADC_0DB), TAG, "write ADC vol1 failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_ADC_VOL2, AK4619_ADC_0DB), TAG, "write ADC vol2 failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_ADC_VOL3, AK4619_ADC_0DB), TAG, "write ADC vol3 failed");

    // Register 0x0D: AD2HPFN/AD1HPFN are active-high disable bits.
    // Write zero to keep ADC soft mutes disabled and both DC-offset HPFs enabled.
    ESP_RETURN_ON_ERROR(write_reg(REG_ADC_MUTE_HPF, 0x00), TAG, "write ADC HPF failed");

    ESP_RETURN_ON_ERROR(write_reg(REG_DAC_VOL0, AK4619_DAC_0DB), TAG, "write DAC vol0 failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_DAC_VOL1, AK4619_DAC_0DB), TAG, "write DAC vol1 failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_DAC_VOL2, AK4619_DAC_0DB), TAG, "write DAC vol2 failed");
    ESP_RETURN_ON_ERROR(write_reg(REG_DAC_VOL3, AK4619_DAC_0DB), TAG, "write DAC vol3 failed");

    return ESP_OK;
}

static esp_err_t power_up_codec() {
    uint8_t val = 0;
    // TRM power-up sequence: configure registers, supply MCLK/BICK/LRCK, then
    // enable ADC/DAC blocks and release internal timing reset.
    ESP_RETURN_ON_ERROR(write_reg(REG_POWER, 0x37), TAG, "write POWER failed");
    ESP_RETURN_ON_ERROR(read_reg(REG_POWER, &val), TAG, "read configured power failed");
    ESP_LOGI(TAG, "Power register after config: 0x%02X", val);
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

bool CodecAk4619::InitCodec() {
    ESP_LOGI(TAG, "Trying AK4619 codec");
    cleanup();

    esp_err_t err = setup_reset_pin();
    if (err != ESP_OK) {
        cleanup();
        return false;
    }

    err = setup_i2c();
    if (err != ESP_OK) {
        cleanup();
        return false;
    }

    err = configure_codec_standby();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AK4619 register configuration failed: %s", esp_err_to_name(err));
        cleanup();
        return false;
    }

    err = setup_i2s();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AK4619 I2S setup failed: %s", esp_err_to_name(err));
        cleanup();
        return false;
    }

    err = power_up_codec();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AK4619 power-up failed: %s", esp_err_to_name(err));
        cleanup();
        return false;
    }

    initialized = true;
    ESP_LOGI(TAG, "AK4619 TDM initialization complete");
    return true;
}

void CodecAk4619::PrintBootTiming() {
    ESP_LOGI(TAG, "AK4619 active, four-channel TDM mode");
}

void CodecAk4619::ADCHighPassEnable() {
    if (!initialized) return;
    uint8_t val = 0;
    if (read_reg(REG_ADC_MUTE_HPF, &val) != ESP_OK) return;
    val &= ~0x06; // AD2HPFN=0, AD1HPFN=0 -> HPF enabled
    write_reg(REG_ADC_MUTE_HPF, val);
}

void CodecAk4619::ADCHighPassDisable() {
    if (!initialized) return;
    uint8_t val = 0;
    if (read_reg(REG_ADC_MUTE_HPF, &val) != ESP_OK) return;
    val |= 0x06; // AD2HPFN=1, AD1HPFN=1 -> HPF disabled
    write_reg(REG_ADC_MUTE_HPF, val);
}

void CodecAk4619::SetOutputLevels(uint32_t left, uint32_t right) {
    if (!initialized) return;
    const uint8_t l = output_level_to_ak_volume(left);
    const uint8_t r = output_level_to_ak_volume(right);
    write_reg(REG_DAC_VOL0, l);
    write_reg(REG_DAC_VOL1, r);
    write_reg(REG_DAC_VOL2, l);
    write_reg(REG_DAC_VOL3, r);
}

void IRAM_ATTR CodecAk4619::ReadBuffer(float *buf, uint32_t frames) {
    if (frames > MAX_CODEC_FRAMES) frames = MAX_CODEC_FRAMES;
    size_t nb = 0;
    i2s_channel_read(rx_handle, i2s_tmp, frames * 4 * sizeof(int32_t), &nb, portMAX_DELAY);

    const float div = 1.0f / 2147483648.0f;
    for (uint32_t i = 0; i < frames * 4; i++) {
        buf[i] = static_cast<float>(i2s_tmp[i]) * div;
    }
}

void IRAM_ATTR CodecAk4619::WriteBuffer(float *buf, uint32_t frames) {
    if (frames > MAX_CODEC_FRAMES) frames = MAX_CODEC_FRAMES;
    size_t nb = 0;
    const float mult = 2147483647.f;

    for (uint32_t i = 0; i < frames * 4; i++) {
        float v = std::max(-1.0f, std::min(1.0f, buf[i]));
        i2s_tmp[i] = static_cast<int32_t>(mult * v);
    }

    i2s_channel_write(tx_handle, i2s_tmp, frames * 4 * sizeof(int32_t), &nb, portMAX_DELAY);
}
