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

#ifndef FLAST_UI_RENDER_H
#define FLAST_UI_RENDER_H

#include <stdint.h>

// ---------------------------------------------------------------------
// Pixel format: RGB_565, 2 bytes per pixel.
//
// This UI draws exactly two colours -- fully opaque black and fully
// opaque white -- and never blends, never uses alpha, and never draws a
// gradient, an image, or an antialiased edge. In RGB_565 those two
// colours are 0x0000 and 0xFFFF: *exactly* representable, bit for bit,
// with no dithering and no visible difference from the 32-bit surface
// this used to allocate.
//
// What it buys: the window's graphics buffers are the single largest
// block of memory the process owns. On a 1080x2340 display, one buffer
// is 1080*2340*4 = 9.6 MiB at 32bpp and 4.8 MiB at 16bpp, and the
// compositor holds more than one of them. `dumpsys meminfo` reported
// EGL mtrack 6642 KB for this app -- halving the bytes per pixel takes
// roughly half of that off the process's PSS, and it is the biggest
// single RAM saving available anywhere in this codebase.
//
// It also halves the memory bandwidth of every full-screen clear and
// every glyph blit, which is CPU (battery) as well as RAM.
//
// To revert to 32-bit: set ui_pixel_t back to uint32_t, UI_WINDOW_FORMAT
// to WINDOW_FORMAT_RGBX_8888, and the two colours to 0xFF000000 /
// 0xFFFFFFFF. Nothing else in the codebase hardcodes a pixel width --
// that is what this typedef is for.
// ---------------------------------------------------------------------
typedef uint16_t ui_pixel_t;

// Matches WINDOW_FORMAT_RGB_565 from <android/native_window.h>. Spelled
// out numerically so this header does not have to drag the NDK window
// header into every file that only wants to draw.
#define UI_WINDOW_FORMAT 4

#define UI_COLOR_BLACK ((ui_pixel_t)0x0000u)
#define UI_COLOR_WHITE ((ui_pixel_t)0xFFFFu)

void ui_render_begin(int width, int height, int stride);

void ui_fill_rect(ui_pixel_t *pixels, int stride, int x, int y, int w, int h, ui_pixel_t color);
void ui_draw_char(ui_pixel_t *pixels, int stride, int x, int y, char c, int scale, ui_pixel_t color);
void ui_draw_text(ui_pixel_t *pixels, int stride, int x, int y, const char *text, int scale, ui_pixel_t color);

// Same as ui_draw_text(), but stops after `max_w` pixels of advance so a
// long filename cannot scribble past the right edge of the screen (the
// per-pixel clip in ui_draw_char() already made that safe, this just
// stops doing the work for glyphs that are entirely off-screen).
void ui_draw_text_clipped(ui_pixel_t *pixels, int stride, int x, int y, const char *text,
                          int scale, ui_pixel_t color, int max_w);

int ui_glyph_w(int scale);
int ui_glyph_h(int scale);
int ui_text_width(const char *text, int scale);

#endif
