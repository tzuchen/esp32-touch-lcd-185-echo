#include "board_lcd.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"

#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_io_expander_tca9554.h"

// Board: ESP-32-Touch-LCD-1.85 (ESP32-S3)
// LCD: ST77916 (QSPI)

static const char *TAG = "board_lcd";

// ---- Pins (from upstream board config) ----
#define QSPI_PIN_NUM_LCD_PCLK   12
#define QSPI_PIN_NUM_LCD_CS     6
#define QSPI_PIN_NUM_LCD_DATA0  0
#define QSPI_PIN_NUM_LCD_DATA1  5
#define QSPI_PIN_NUM_LCD_DATA2  16
#define QSPI_PIN_NUM_LCD_DATA3  13
#define QSPI_PIN_NUM_LCD_RST    -1  // reset via TCA9554
#define QSPI_PIN_NUM_BK_LIGHT   46

#define I2C_MASTER_SCL_IO       10
#define I2C_MASTER_SDA_IO       11
#define I2C_MASTER_PORT         0
#define I2C_MASTER_FREQ_HZ      400000

#define TCA9554_I2C_ADDRESS     ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000
#define IO_EXPANDER_PIN_NUM_RST 0

#define LCD_H_RES               360
#define LCD_V_RES               360

// Minimal vendor init for ST77916 variant used by this board.
static const esp_lcd_panel_vendor_init_cmd_t vendor_init_cmds[] = {
    {0xF0, (uint8_t[]){0xC3}, 1, 0},
    {0xF0, (uint8_t[]){0x96}, 1, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x65}, 1, 0},
    {0xB4, (uint8_t[]){0x01}, 1, 0},
    {0xB7, (uint8_t[]){0xC6}, 1, 0},
    {0xC6, (uint8_t[]){0x0F}, 1, 0},
    {0xE8, (uint8_t[]){0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0},
    {0xC1, (uint8_t[]){0x06}, 1, 0},
};

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;

static esp_err_t tca9554_reset_pulse(void)
{
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_MASTER_PORT,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "i2c_new_master_bus");

    esp_io_expander_handle_t expander = NULL;
    esp_io_expander_i2c_config_t exp_cfg = {
        .i2c_host = bus,
        .i2c_address = TCA9554_I2C_ADDRESS,
    };
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca9554(&exp_cfg, &expander), TAG, "new tca9554");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(expander, IO_EXPANDER_PIN_NUM_RST, IO_EXPANDER_OUTPUT), TAG, "set dir");

    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_RST, 0), TAG, "rst low");
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_RST, 1), TAG, "rst high");
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_io_expander_del(expander);
    i2c_del_master_bus(bus);
    return ESP_OK;
}

esp_err_t board_lcd_init(void)
{
    ESP_LOGI(TAG, "init LCD");

    // Backlight
    gpio_config_t bk = {
        .pin_bit_mask = 1ULL << QSPI_PIN_NUM_BK_LIGHT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&bk));
    gpio_set_level(QSPI_PIN_NUM_BK_LIGHT, 0);

    // Reset (via IO expander)
    ESP_RETURN_ON_ERROR(tca9554_reset_pulse(), TAG, "tca9554 reset");

    // QSPI (SPI2 host)
    spi_bus_config_t buscfg = {
        .sclk_io_num = QSPI_PIN_NUM_LCD_PCLK,
        .data0_io_num = QSPI_PIN_NUM_LCD_DATA0,
        .data1_io_num = QSPI_PIN_NUM_LCD_DATA1,
        .data2_io_num = QSPI_PIN_NUM_LCD_DATA2,
        .data3_io_num = QSPI_PIN_NUM_LCD_DATA3,
        .max_transfer_sz = LCD_H_RES * 40 * 2, // modest chunk
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus init");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = QSPI_PIN_NUM_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &s_io), TAG, "new_panel_io_spi");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &(st77916_vendor_config_t){
            .init_cmds = vendor_init_cmds,
            .init_cmds_size = sizeof(vendor_init_cmds) / sizeof(vendor_init_cmds[0]),
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77916(s_io, &panel_cfg, &s_panel), TAG, "new_panel_st77916");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel_reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel_init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "swap_xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp_on");

    gpio_set_level(QSPI_PIN_NUM_BK_LIGHT, 1);
    return ESP_OK;
}

esp_err_t board_lcd_flush(int x1, int y1, int x2, int y2, const void *color_data)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    return esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, color_data);
}
