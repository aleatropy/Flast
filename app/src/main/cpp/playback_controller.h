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

#ifndef FLAST_PLAYBACK_CONTROLLER_H
#define FLAST_PLAYBACK_CONTROLLER_H

#include <stdbool.h>

#include "music_library.h"

typedef struct {
    int queue_size;
    int current_index;
    char current_path[512];
    bool is_playing;
    bool is_paused;
    bool is_bit_perfect_native;
    bool has_pending_nav;
    int pending_preview_index;
    char pending_preview_path[512];

    double position_seconds;
} PlaybackSnapshot;

void pc_init(void);

// Takes ownership of `*queue` and zeroes it, so the caller cannot keep
// using memory this module now owns. The queue arrives as a packed
// StrList (one arena + one index array), which means switching to a
// 3,000-track queue costs two allocations instead of 3,001: the previous
// interface took a char** of individually strdup()'d strings, so every
// "play all tracks" tap malloc'd once per track and paid ~16 bytes of
// allocator header on each one.
void pc_set_queue(StrList *queue, int start_index);

void pc_get_snapshot(PlaybackSnapshot *out);

void pc_play_index(int index);

void pc_toggle_play_pause(void);
void pc_stop(void);

void pc_request_next(void);
void pc_request_prev(void);

void pc_tick(void);

int pc_ms_until_next_tick(void);

void pc_on_track_finished(void);

void pc_set_dac_blocked(bool blocked);

// Audio device changed (USB DAC plugged or unplugged): pause, keeping the
// track and its position so the user can carry on where they were.
void pc_pause_for_device_change(void);

bool pc_consume_dirty(void);

#endif
