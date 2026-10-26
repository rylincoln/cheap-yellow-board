#include "glance.h"

#include <string.h>

#include "cyd_display.h"
#include "esp_log.h"

#define STRIP_ROWS 16

static const char *TAG = "glance";

static uint16_t strip[CYD_LCD_MAX_WIDTH * STRIP_ROWS];
/* The tiles on screen, byte for byte; zeroed when a draw fails. */
static glance_tile_t drawn[GLANCE_TILE_COUNT];

/* Draws one tile a strip at a time: each strip starts as the tile's
   background, then every text crossing it is blended in. */
static esp_err_t draw_tile(const glance_tile_t *tile)
{
    const rect_t *rect = &tile->rect;
    if (rect->w <= 0 || rect->h <= 0 || rect->w > CYD_LCD_MAX_WIDTH) {
        return ESP_ERR_INVALID_ARG;
    }
    int limit = (int)(sizeof(tile->texts) / sizeof(tile->texts[0]));
    int count = tile->count < limit ? tile->count : limit;
    for (int top = rect->y; top < rect->y + rect->h; top += STRIP_ROWS) {
        int rows = rect->y + rect->h - top < STRIP_ROWS ? rect->y + rect->h - top : STRIP_ROWS;
        size_t pixels = (size_t)rect->w * rows;
        for (size_t index = 0; index < pixels; ++index) {
            strip[index] = tile->background;
        }
        for (int index = 0; index < count; ++index) {
            const glance_text_t *text = &tile->texts[index];
            /* Glyph ink stays inside the line, so only lines crossing the strip matter. */
            if (text->line_y < top + rows && text->line_y + text->font->line_height > top) {
                bigfont_render(text->font, text->text, text->x - rect->x, text->line_y,
                               text->color, strip, rect->w, top, rows);
            }
        }
        esp_err_t result = cyd_draw_bitmap(rect->x, top, rect->w, rows, strip);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_OK;
}

void glance_draw(const glance_tile_t tiles[GLANCE_TILE_COUNT], bool full)
{
    if (full) {
        esp_err_t result = cyd_fill_rect(0, 0, cyd_lcd_width(), cyd_lcd_height(), CYD_COLOR_BLACK);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Could not clear the screen: %s", esp_err_to_name(result));
        }
    }
    for (int index = 0; index < GLANCE_TILE_COUNT; ++index) {
        if (!full && memcmp(&drawn[index], &tiles[index], sizeof(drawn[index])) == 0) {
            continue;
        }
        esp_err_t result = draw_tile(&tiles[index]);
        if (result == ESP_OK) {
            /* memcpy rather than assignment, so padding is copied too. */
            memcpy(&drawn[index], &tiles[index], sizeof(drawn[index]));
        } else {
            memset(&drawn[index], 0, sizeof(drawn[index]));
            ESP_LOGE(TAG, "Could not draw tile %d: %s", index, esp_err_to_name(result));
        }
    }
}
