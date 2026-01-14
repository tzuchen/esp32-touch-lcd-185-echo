#include "ota_github.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_check.h"

static const char *TAG = "ota";

static esp_err_t http_get_text(const char *url, char *out, size_t out_len)
{
    if (!url || !url[0]) return ESP_ERR_INVALID_ARG;

    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(client, ESP_FAIL, TAG, "http_client_init failed");

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "HTTP status %d", status);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int rlen = esp_http_client_read_response(client, out, (int)out_len - 1);
    if (rlen < 0) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    out[rlen] = 0;

    // trim whitespace
    while (rlen > 0 && (out[rlen-1] == '\n' || out[rlen-1] == '\r' || out[rlen-1] == ' ' || out[rlen-1] == '\t')) {
        out[--rlen] = 0;
    }

    esp_http_client_cleanup(client);
    return ESP_OK;
}

esp_err_t ota_github_check_and_update(void)
{
#if !CONFIG_OTA_GITHUB_ENABLE
    ESP_LOGI(TAG, "OTA disabled (menuconfig)");
    return ESP_OK;
#else
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "Current version: %s", desc->version);

    if (strlen(CONFIG_OTA_VERSION_URL) == 0 || strlen(CONFIG_OTA_FIRMWARE_URL) == 0) {
        ESP_LOGW(TAG, "OTA URLs not set; skipping");
        return ESP_OK;
    }

    char remote_ver[64] = {0};
    ESP_RETURN_ON_ERROR(http_get_text(CONFIG_OTA_VERSION_URL, remote_ver, sizeof(remote_ver)), TAG, "failed to fetch remote version");

    ESP_LOGI(TAG, "Remote version: %s", remote_ver);
    if (strcmp(remote_ver, desc->version) == 0) {
        ESP_LOGI(TAG, "Already up to date");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Update available -> starting OTA");

    esp_http_client_config_t http_cfg = {
        .url = CONFIG_OTA_FIRMWARE_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "OTA success; rebooting");
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    }

    ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    return err;
#endif
}
