#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Init LCD hardware + RGB565 framebuffer flushing.
// After board_lcd_init(), you can call board_lcd_flush(x1,y1,x2,y2,buf).
esp_err_t board_lcd_init(void);

// Flush a rectangle (x2/y2 are exclusive like esp_lcd_draw_bitmap).
esp_err_t board_lcd_flush(int x1, int y1, int x2, int y2, const void *color_data);

int board_lcd_width(void);
int board_lcd_height(void);

#ifdef __cplusplus
}
#endif
