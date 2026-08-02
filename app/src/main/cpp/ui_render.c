/*
 * Flast — a bit-perfect FLAC player for Android.
 * Copyright (C) 2026 Aleatropy and the Flast contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Bundles libFLAC (decoder only), BSD-3-Clause, (C) Xiph.Org Foundation —
 * see app/src/main/cpp/third_party/flac-1.4.3/COPYING.Xiph
 */

#include "ui_render.h"
#include "ui_font_spleen8x16.h"

#include <stddef.h>
#include <string.h>

static int g_buf_w = 0;
static int g_buf_h = 0;

void ui_render_begin(int width, int height, int stride) {
    (void)stride;
    g_buf_w = width;
    g_buf_h = height;
}

void ui_fill_rect(ui_pixel_t *pixels, int stride, int x, int y, int w, int h, ui_pixel_t color) {
    if (pixels == NULL || w <= 0 || h <= 0) return;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > g_buf_w) x1 = g_buf_w;
    if (y1 > g_buf_h) y1 = g_buf_h;
    if (x0 >= x1 || y0 >= y1) return;

    size_t run = (size_t)(x1 - x0);

    // Both colours this UI ever uses (0x0000 and 0xFFFF) have identical
    // high and low bytes, so a whole run collapses into one memset --
    // which on every Android ABI is a hand-written, vectorised libc
    // routine. That matters because the very first thing every frame
    // does is clear the entire window: ~2.5 million pixels on a 1080p
    // phone. The generic loop below stays for correctness if a third
    // colour is ever added.
    if ((color & 0xFFu) == (color >> 8)) {
        int fill = (int)(color & 0xFFu);
        for (int yy = y0; yy < y1; yy++) {
            memset(pixels + (size_t)yy * (size_t)stride + (size_t)x0, fill, run * sizeof(ui_pixel_t));
        }
        return;
    }

    for (int yy = y0; yy < y1; yy++) {
        ui_pixel_t *row = pixels + (size_t)yy * (size_t)stride + (size_t)x0;
        for (size_t xx = 0; xx < run; xx++) {
            row[xx] = color;
        }
    }
}

// Blits one glyph. The previous version called ui_fill_rect() once per
// lit pixel of the glyph -- for an 8x16 font at scale 2 that is up to
// 128 function calls, each redoing the full rectangle clip, to draw one
// character. This walks the destination rows directly with the clip
// computed once, which is both smaller code and roughly an order of
// magnitude less work per frame on a screen full of text.
void ui_draw_char(ui_pixel_t *pixels, int stride, int x, int y, char c, int scale, ui_pixel_t color) {
    if (pixels == NULL) return;
    if (scale < 1) scale = 1;

    int glyph_w = SPLEEN_GLYPH_W * scale;
    int glyph_h = SPLEEN_GLYPH_H * scale;
    if (x + glyph_w <= 0 || x >= g_buf_w || y + glyph_h <= 0 || y >= g_buf_h) {
        return;
    }

    const uint8_t *glyph = spleen_get_glyph((unsigned char)c);

    for (int row = 0; row < SPLEEN_GLYPH_H; row++) {
        uint8_t bits = glyph[row];
        if (bits == 0) continue;

        int py0 = y + row * scale;
        int py1 = py0 + scale;
        if (py0 < 0) py0 = 0;
        if (py1 > g_buf_h) py1 = g_buf_h;
        if (py0 >= py1) continue;

        for (int col = 0; col < SPLEEN_GLYPH_W; col++) {
            if ((bits & (0x80u >> col)) == 0) continue;

            int px0 = x + col * scale;
            int px1 = px0 + scale;
            if (px0 < 0) px0 = 0;
            if (px1 > g_buf_w) px1 = g_buf_w;
            if (px0 >= px1) continue;

            for (int py = py0; py < py1; py++) {
                ui_pixel_t *dst = pixels + (size_t)py * (size_t)stride + (size_t)px0;
                for (int px = px0; px < px1; px++) {
                    *dst++ = color;
                }
            }
        }
    }
}

void ui_draw_text(ui_pixel_t *pixels, int stride, int x, int y, const char *text, int scale, ui_pixel_t color) {
    ui_draw_text_clipped(pixels, stride, x, y, text, scale, color, g_buf_w - x);
}

void ui_draw_text_clipped(ui_pixel_t *pixels, int stride, int x, int y, const char *text,
                          int scale, ui_pixel_t color, int max_w) {
    if (text == NULL) return;
    if (scale < 1) scale = 1;

    int gw = SPLEEN_GLYPH_W * scale;
    int gh = SPLEEN_GLYPH_H * scale;
    int cx = x;
    int cy = y;
    int limit = x + max_w;

    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '\n') {
            cx = x;
            cy += gh;
            if (cy >= g_buf_h) return;
            continue;
        }
        if (cx >= limit || cx >= g_buf_w) {
            // Rest of this line is off-screen: skip to the next one
            // instead of blitting glyphs nobody can see.
            const char *nl = strchr(p, '\n');
            if (nl == NULL) return;
            p = nl;
            cx = x;
            cy += gh;
            if (cy >= g_buf_h) return;
            continue;
        }
        ui_draw_char(pixels, stride, cx, cy, *p, scale, color);
        cx += gw;
    }
}

int ui_glyph_w(int scale) {
    if (scale < 1) scale = 1;
    return SPLEEN_GLYPH_W * scale;
}

int ui_glyph_h(int scale) {
    if (scale < 1) scale = 1;
    return SPLEEN_GLYPH_H * scale;
}

int ui_text_width(const char *text, int scale) {
    if (text == NULL) return 0;
    int gw = ui_glyph_w(scale);
    int max_w = 0;
    int cur_w = 0;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '\n') {
            if (cur_w > max_w) max_w = cur_w;
            cur_w = 0;
            continue;
        }
        cur_w += gw;
    }
    return cur_w > max_w ? cur_w : max_w;
}
