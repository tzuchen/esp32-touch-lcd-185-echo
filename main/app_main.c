#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_err.h"

#include "board_lcd.h"
#include "gfx.h"
#include "wifi_simple.h"
#include "ota_github.h"
#include "audio_echo.h"

static const char *TAG = "app";

static void show_status(const char *line1, const char *line2)
{
    gfx_clear(0x0000);
    gfx_draw_string(8, 12, line1);
    if (line2) gfx_draw_string(8, 28, line2);
    gfx_flush();
}

void app_main(void)
{
    esp_err_t err;

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(board_lcd_init());
    gfx_init(board_lcd_get_panel());

    show_status("Booting...", "LCD OK");
    ESP_LOGI(TAG, "LCD init ok");

    show_status("WiFi...", "connecting");
    ESP_ERROR_CHECK(wifi_simple_init_and_connect());

    show_status("WiFi connected", wifi_simple_get_ip_str());

#if CONFIG_OTA_GITHUB_ENABLE
    show_status("OTA check...", "GitHub");
    ota_github_try_update();
#endif

    show_status("Audio...", "echo running");
    ESP_ERROR_CHECK(audio_echo_start());

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
