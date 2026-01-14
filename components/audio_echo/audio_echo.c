#include "audio_echo.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_std.h"

// Pins (board: ESP-32-Touch-LCD-1.85)
#define MIC_WS   2
#define MIC_SCK  15
#define MIC_SD   39

#define SPK_DOUT 47
#define SPK_BCLK 48
#define SPK_LRCK 38

#define SAMPLE_RATE 16000
#define FRAMES_PER_CHUNK 256

static const char *TAG = "echo";

static i2s_chan_handle_t tx_chan;
static i2s_chan_handle_t rx_chan;

static void echo_task(void *arg)
{
    (void)arg;
    int16_t buf[FRAMES_PER_CHUNK];
    size_t bytes_read = 0;
    size_t bytes_written = 0;

    while (1) {
        esp_err_t err = i2s_channel_read(rx_chan, buf, sizeof(buf), &bytes_read, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Optional: soft limiter / gain.
        for (int i = 0; i < (int)(bytes_read / sizeof(int16_t)); i++) {
            int32_t v = buf[i];
            v = (v * 2); // ~+6dB
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            buf[i] = (int16_t)v;
        }

        err = i2s_channel_write(tx_chan, buf, bytes_read, &bytes_written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s write failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

esp_err_t audio_echo_start(void)
{
    // TX: I2S0
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    tx_chan_cfg.auto_clear = true;

    // RX: I2S1
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    rx_chan_cfg.auto_clear = true;

    ESP_RETURN_ON_ERROR(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL), TAG, "new tx channel");
    ESP_RETURN_ON_ERROR(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan), TAG, "new rx channel");

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_BCLK,
            .ws = SPK_LRCK,
            .dout = SPK_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_SCK,
            .ws = MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    // Mic is often right-only; select right.
    rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg), TAG, "init tx std");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg), TAG, "init rx std");

    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_chan), TAG, "enable tx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_chan), TAG, "enable rx");

    xTaskCreatePinnedToCore(echo_task, "echo_task", 4096, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "audio echo started (%d Hz, mono 16-bit)", SAMPLE_RATE);
    return ESP_OK;
}
