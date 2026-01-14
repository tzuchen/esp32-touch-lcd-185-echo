#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gfx_init(void);
void gfx_clear(uint16_t rgb565);
void gfx_flush_full(void);
void gfx_draw_string(int x, int y, const char *s);

#ifdef __cplusplus
}
#endif
