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

#ifndef FLAST_UI_FONT_SPLEEN8X16_H
#define FLAST_UI_FONT_SPLEEN8X16_H

#include <stdint.h>

#define SPLEEN_GLYPH_W 8
#define SPLEEN_GLYPH_H 16
#define SPLEEN_FIRST_CP 0x20
#define SPLEEN_LAST_CP 0x7E

const uint8_t *spleen_get_glyph(unsigned char c);

#endif
