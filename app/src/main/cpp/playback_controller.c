#define _POSIX_C_SOURCE 200809L

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

#include "playback_controller.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    FLAST_OK = 0,
    FLAST_ERROR_FILE_OPEN = 1,
    FLAST_ERROR_AAUDIO_OPEN = 2,
    // FLAST_ERROR_MMAP_BLOCKED (3) is gone on purpose. flac_stream.c
    // used to return it whenever AAudio_getMMapPolicy() said this
    // device will never grant an MMAP path -- and that error meant the
    // app played nothing at all on such a device. Per spec 3.6 that is
    // a BIT-PERFECT: NO state, not a refusal to play, so the audio
    // layer now falls back to SHARED and this result no longer exists.
} FlastResult;

extern FlastResult flast_stream_play(const char *filepath, bool *out_is_bit_perfect,
                                      int32_t *out_sample_rate, int32_t *out_bits);
extern void flast_stream_stop(void);
extern void flast_stream_prefetch(const char *filepath);
extern void flast_stream_pause(void);
extern bool flast_stream_resume(void);
extern double flast_stream_get_position_seconds(void);
extern bool flast_stream_is_bit_perfect_now(void);
extern int flast_stream_bit_perfect_reason(void);

#define NAV_DEBOUNCE_MS 280LL
#define PREV_RESTART_THRESHOLD_SECONDS 3.0

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static StrList g_queue = {0};
static int g_current_index = -1;

#define g_queue_count (g_queue.count)

static bool g_is_playing = false;
static bool g_is_paused = false;
static bool g_is_bit_perfect_native = false;
static bool g_dac_blocked = false;

static bool g_has_pending_nav = false;
static int g_pending_index = 0;
static long long g_nav_deadline_ms = 0;

// True once playback has run off the end of the queue. The queue is then
// rewound to the first track WITHOUT playing it, so the player shows
// where playback would resume rather than sitting on the track that just
// ended. This flag is what makes the transport controls behave sensibly
// from that resting point: next goes to the first track (not the second),
// previous goes to the last.
static bool g_queue_ended = false;

static bool g_dirty = true;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void free_queue_locked(void) {
    ml_str_list_free(&g_queue);
}

static int wrap_index_locked(int index) {
    if (g_queue_count == 0) return -1;
    int m = index % g_queue_count;
    if (m < 0) m += g_queue_count;
    return m;
}

static void switch_to_index_locked(int index) {
    if (g_queue_count == 0) return;
    g_queue_ended = false;
    g_current_index = wrap_index_locked(index);

    if (g_dac_blocked) {

        g_is_playing = false;
        g_is_paused = false;
        g_dirty = true;
        return;
    }

    // Deliberately NOT calling flast_stream_stop() first. It closes the
    // AAudioStream, which made `previous_sample_rate` always -1 inside
    // flast_stream_play() and so forced a full stream re-open between
    // every pair of tracks -- the "same parameters, reuse the stream"
    // path existed but could never be reached. Re-opening per track is
    // both a longer gap between tracks and a fresh EXCLUSIVE
    // negotiation each time, which on a flaky HAL can succeed on track
    // 1 and quietly degrade to SHARED on track 2. flast_stream_play()
    // already tears down the decoder, the decode thread and the ring
    // buffer itself, and stops the stream before touching anything the
    // audio callback reads.
    bool is_bp = false;
    int32_t sr = 0;
    int32_t bits = 0;
    FlastResult r = flast_stream_play(g_queue.items[g_current_index], &is_bp, &sr, &bits);
    g_is_bit_perfect_native = is_bp;
    g_is_playing = (r == FLAST_OK);
    g_is_paused = false;
    g_dirty = true;

    // Pull the front of the next track through the page cache now, while
    // there are minutes of music left to cover it, so that the hand-over
    // at the end of this track does not have to pay for a cold open.
    // Fire-and-forget: it cannot fail in a way that matters.
    if (g_is_playing && g_current_index + 1 < g_queue_count) {
        flast_stream_prefetch(g_queue.items[g_current_index + 1]);
    }
}

static void schedule_nav_locked(int new_index) {
    if (g_queue_count == 0) return;
    g_pending_index = new_index;
    g_has_pending_nav = true;
    g_nav_deadline_ms = now_ms() + NAV_DEBOUNCE_MS;
    g_dirty = true;
}

void pc_init(void) {
    pthread_mutex_lock(&g_lock);
    free_queue_locked();
    g_current_index = -1;
    g_is_playing = false;
    g_is_paused = false;
    g_is_bit_perfect_native = false;
    g_dac_blocked = false;
    g_has_pending_nav = false;
    g_queue_ended = false;
    g_dirty = true;
    pthread_mutex_unlock(&g_lock);
}

void pc_set_queue(StrList *queue, int start_index) {
    pthread_mutex_lock(&g_lock);
    free_queue_locked();
    if (queue != NULL && queue->count > 0) {
        // Ownership transfer, not a copy: the caller built one packed
        // arena for this queue and hands it over whole, so there is
        // exactly one heap copy of the queue's path text in the process.
        g_queue = *queue;
        memset(queue, 0, sizeof(*queue));
        if (start_index < 0) start_index = 0;
        if (start_index >= g_queue.count) start_index = g_queue.count - 1;
        g_current_index = start_index;
    } else {
        if (queue != NULL) ml_str_list_free(queue);
        g_current_index = -1;
    }
    g_queue_ended = false;
    g_has_pending_nav = false;
    g_dirty = true;
    pthread_mutex_unlock(&g_lock);
}

void pc_get_snapshot(PlaybackSnapshot *out) {
    if (out == NULL) return;
    bool need_position;

    pthread_mutex_lock(&g_lock);
    out->queue_size = g_queue_count;
    out->current_index = g_current_index;
    if (g_current_index >= 0 && g_current_index < g_queue_count) {
        strncpy(out->current_path, g_queue.items[g_current_index], sizeof(out->current_path) - 1);
        out->current_path[sizeof(out->current_path) - 1] = '\0';
    } else {
        out->current_path[0] = '\0';
    }
    out->is_playing = g_is_playing;
    out->is_paused = g_is_paused;
    // Queried live rather than read from a variable set when the track
    // opened -- see flast_stream_is_bit_perfect_now(). Done inside the
    // lock because that is what serialises against the stream being torn
    // down on another thread.
    out->bit_perfect_reason = g_is_playing ? flast_stream_bit_perfect_reason()
                                           : (int)BP_REASON_NOT_PLAYING;
    out->is_bit_perfect_native = (out->bit_perfect_reason == (int)BP_REASON_OK);
    out->has_pending_nav = g_has_pending_nav;
    out->pending_preview_index = g_has_pending_nav ? wrap_index_locked(g_pending_index) : -1;
    if (out->has_pending_nav && out->pending_preview_index >= 0 &&
        out->pending_preview_index < g_queue_count) {
        strncpy(out->pending_preview_path, g_queue.items[out->pending_preview_index],
                sizeof(out->pending_preview_path) - 1);
        out->pending_preview_path[sizeof(out->pending_preview_path) - 1] = '\0';
    } else {
        out->pending_preview_path[0] = '\0';
    }
    need_position = g_is_playing;
    pthread_mutex_unlock(&g_lock);

    out->position_seconds = need_position ? flast_stream_get_position_seconds() : 0.0;
}

void pc_play_index(int index) {
    pthread_mutex_lock(&g_lock);
    switch_to_index_locked(index);
    pthread_mutex_unlock(&g_lock);
}

void pc_toggle_play_pause(void) {
    pthread_mutex_lock(&g_lock);
    if (!g_is_playing && !g_is_paused) {
        if (g_current_index < 0 && g_queue_count > 0) {
            g_current_index = 0;
        }
        switch_to_index_locked(g_current_index);
    } else if (g_is_playing && !g_is_paused) {
        flast_stream_pause();
        g_is_paused = true;
        g_dirty = true;
    } else if (g_is_paused) {
        // Still waiting on the DAC volume question: the modal screen
        // means the user cannot normally reach this control, but the
        // block is a safety rule and should not depend on UI routing to
        // be enforced.
        if (!g_dac_blocked) {
            // Only clear the paused flag if playback ACTUALLY restarted.
            // Otherwise the button would flip to PAUSE over silence.
            if (flast_stream_resume()) {
                g_is_paused = false;
            }
            g_dirty = true;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

void pc_stop(void) {
    pthread_mutex_lock(&g_lock);
    flast_stream_stop();
    g_is_playing = false;
    g_is_paused = false;
    g_has_pending_nav = false;
    g_dirty = true;
    pthread_mutex_unlock(&g_lock);
}

void pc_request_next(void) {
    pthread_mutex_lock(&g_lock);
    if (g_queue_ended) {
        // Resting at the end of the queue: the first track IS the next
        // one. Advancing from here must not skip it.
        schedule_nav_locked(0);
    } else {
        int base = g_has_pending_nav ? g_pending_index : g_current_index;
        schedule_nav_locked(base + 1);
    }
    pthread_mutex_unlock(&g_lock);
}

void pc_request_prev(void) {
    pthread_mutex_lock(&g_lock);
    if (g_queue_ended) {
        // Going back from the resting point means the last track.
        schedule_nav_locked(g_queue_count - 1);
    } else if (g_has_pending_nav) {
        schedule_nav_locked(g_pending_index - 1);
    } else if (g_is_playing && flast_stream_get_position_seconds() >= PREV_RESTART_THRESHOLD_SECONDS) {

        schedule_nav_locked(g_current_index);
    } else {
        schedule_nav_locked(g_current_index - 1);
    }
    pthread_mutex_unlock(&g_lock);
}

void pc_tick(void) {
    pthread_mutex_lock(&g_lock);
    if (g_has_pending_nav && now_ms() >= g_nav_deadline_ms) {
        int idx = g_pending_index;
        g_has_pending_nav = false;
        switch_to_index_locked(idx);
    }
    pthread_mutex_unlock(&g_lock);
}

int pc_ms_until_next_tick(void) {
    pthread_mutex_lock(&g_lock);
    int result = -1;
    if (g_has_pending_nav) {
        long long remaining = g_nav_deadline_ms - now_ms();
        result = (remaining > 0) ? (int)remaining : 0;
    }
    pthread_mutex_unlock(&g_lock);
    return result;
}

void pc_on_track_finished(void) {
    pthread_mutex_lock(&g_lock);
    if (g_current_index >= g_queue_count - 1) {
        // End of the album/playlist. Stop, then rewind to the first track
        // without starting it, so the player is showing what play would
        // begin rather than leaving the finished track on screen as if it
        // were still current.
        flast_stream_stop();
        g_is_playing = false;
        g_is_paused = false;
        g_current_index = g_queue_count > 0 ? 0 : -1;
        g_queue_ended = true;
        g_dirty = true;
    } else {
        switch_to_index_locked(g_current_index + 1);
    }
    pthread_mutex_unlock(&g_lock);
}

// Called whenever the audio device changes under us -- a USB DAC plugged
// in or pulled out. Both directions PAUSE rather than stop.
//
// It used to call flast_stream_stop(), which tore down the decoder, the
// stream and the ring buffer. That reset frames_played, so the clock
// snapped back to 00:00 and the track restarted from the beginning when
// you pressed play. Pausing is just as safe -- no audio is produced
// either way, which is the entire point when system volume is pinned to
// maximum for a DAC that is no longer there -- and it keeps the decoder
// and its position alive so play resumes exactly where it stopped.
//
// The cost is ~250-320 KB held while paused (ring buffer plus decoder
// state) that used to be freed. That is ~2% of the app's footprint, and
// it buys a resumable timestamp.
void pc_pause_for_device_change(void) {
    pthread_mutex_lock(&g_lock);
    if (g_is_playing && !g_is_paused) {
        flast_stream_pause();
        g_is_paused = true;
        g_has_pending_nav = false;
        g_dirty = true;
    }
    pthread_mutex_unlock(&g_lock);
}

// Spec 3.5 blocks playback until the DAC volume question is answered,
// on hearing-safety grounds. This used to only gate *starting* a track,
// which left the exact case the rule exists for uncovered: music already
// playing when an unknown DAC is plugged in kept going, and was now
// routed to that DAC at whatever the system volume happened to be.
//
// Pause, not stop, so the track and position survive. Deliberately no
// auto-resume when the block lifts: answering YES pins STREAM_MUSIC to
// 100%, and resuming straight into that on a DAC whose own knob is
// already up is the hazard, not the fix. The user presses play.
void pc_set_dac_blocked(bool blocked) {
    pthread_mutex_lock(&g_lock);
    if (blocked && !g_dac_blocked && g_is_playing && !g_is_paused) {
        flast_stream_pause();
        g_is_paused = true;
    }
    g_dac_blocked = blocked;
    g_dirty = true;
    pthread_mutex_unlock(&g_lock);
}

bool pc_consume_dirty(void) {
    pthread_mutex_lock(&g_lock);
    bool was_dirty = g_dirty;
    g_dirty = false;
    pthread_mutex_unlock(&g_lock);
    return was_dirty;
}
