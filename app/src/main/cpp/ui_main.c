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

#include <android/configuration.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/native_window.h>
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "music_library.h"
#include "playback_controller.h"
#include "ui_render.h"

#define LOG_TAG "FlastUI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

extern int jni_get_bit_perfect_state(struct android_app *app, bool is_bit_perfect_native);
extern char *jni_get_bit_perfect_reason(struct android_app *app, bool is_bit_perfect_native);
extern bool jni_get_files_dir(struct android_app *app, char *out, size_t out_sz);
extern int jni_get_storage_roots(struct android_app *app, char ***out);
extern void jni_free_storage_roots(char **roots, int count);
extern void native_bridge_reset_env(void);
extern void native_bridge_detach_env(struct android_app *app);
extern bool native_bridge_consume_rescan_request(void);
extern bool native_bridge_has_storage_permission(void);
extern bool native_bridge_dac_question_pending(void);
extern void native_bridge_answer_dac_question(struct android_app *app, bool has_own_control);

typedef enum {
    SCREEN_MUSICA,
    SCREEN_PLAYLISTS,
    SCREEN_CONFIG,
    SCREEN_PLAYER,
    // Modal. Spec 3.5 blocks playback until this is answered, so unlike
    // every other screen it cannot be navigated away from -- the top nav
    // and the bottom player bar are not drawn and do not accept taps
    // while it is up. This is the native replacement for the
    // AlertDialog; see native_bridge.c for why that had to go.
    SCREEN_DAC_QUESTION,
} Screen;

#define ALBUM_NAME_MAX 192

typedef struct {
    struct android_app *app;
    ANativeWindow *window;
    int width, height, stride;

    Screen screen;
    int theme_black;
    int font_scale;
    float density;

    // ------------------------------------------------------------------
    // Library memory. `library` owns the one and only copy of every
    // track path, packed back-to-back in a single arena (see
    // music_library.h). `music_view` is what the MUSIC screen is
    // currently showing:
    //
    //   mode 0 (all tracks)   -- nothing is allocated at all; the code
    //                            reads straight out of `library`.
    //   mode 1 (album list)   -- a small arena of unique folder names.
    //   mode 2 (one album)    -- an index array pointing back into
    //                            `library`'s arena; no path text is
    //                            copied.
    //
    // The version this replaces kept FOUR full arrays -- master paths,
    // master labels, view paths, view labels -- every one of them a
    // separate strdup() per row. Showing "all tracks" therefore held
    // four copies of the library at once. Everything below holds one.
    // ------------------------------------------------------------------
    StrList library;
    StrList music_view;
    int music_offset;
    int musica_selected;
    int musica_picking_playlist;
    int musica_mode; // 0 = all tracks, 1 = album list, 2 = one album's tracks
    char musica_current_album[ALBUM_NAME_MAX];

    // PLAYLISTS screen rows: either playlist names or, once one is
    // opened, that playlist's track paths.
    StrList rows;
    int row_offset;
    int rows_are_tracks;  // label is the filename, not the whole entry
    int rows_have_create; // a synthetic "+ NEW PLAYLIST" row at index 0

    int playlists_viewing;
    char playlists_viewed_name[128];

    char status_msg[96];

    Screen player_return_screen;
    Screen dac_return_screen;

    char bp_reason_msg[256];
    long long bp_reason_hide_at_ms;

    int touch_down_active;
    float touch_down_y;
    long long touch_down_time_ms;

    int ui_dirty;

    // One playback snapshot per frame instead of one per widget. Each
    // snapshot is ~1KB of struct copy taken under the playback mutex,
    // and the old code took three or four of them per redraw.
    PlaybackSnapshot snap;
} AppState;

static AppState g_state;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static const char *filename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// ---------------------------------------------------------------------
// Background library scan.
//
// The scan used to run inline, before the first frame was ever drawn:
// android_main() called into Kotlin, which walked every storage volume
// with File.walkTopDown(), and only when that returned did the UI
// appear. On the development phone with a tidy Music folder that is
// imperceptible. On a device with 40,000 photos, a few games' worth of
// Android/data, and an SD card, it is tens of seconds of a black
// screen -- indistinguishable, from the user's side, from an app that
// does not open on their device.
//
// It now runs on its own thread while the UI is live and interactive,
// and the result is swapped in when it lands.
// ---------------------------------------------------------------------
static atomic_bool g_scan_running = false;
static atomic_bool g_scan_done = false;
static StrList g_scan_result;

static void *scan_thread_func(void *arg) {
    (void)arg;
    StrList found;
    if (!ml_scan(&found)) {
        memset(&found, 0, sizeof(found));
    }
    g_scan_result = found;
    atomic_store_explicit(&g_scan_done, true, memory_order_release);
    atomic_store(&g_scan_running, false);
    return NULL;
}

static void start_scan_if_idle(void) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_scan_running, &expected, true)) {
        return; // already scanning
    }
    // The CAS above guarantees no scan thread is live right now, so
    // g_scan_result belongs to this (UI) thread alone and an earlier
    // result nobody picked up can be dropped here. Doing it on the scan
    // thread instead would race consume_scan_result().
    if (atomic_load(&g_scan_done)) {
        atomic_store(&g_scan_done, false);
        ml_str_list_free(&g_scan_result);
    }
    // Refresh the volume list HERE, on the UI thread, not on the scan
    // thread: this is the thread with an attached JNIEnv, and doing it
    // per scan is what makes an SD card inserted after launch show up.
    char **roots = NULL;
    int root_count = jni_get_storage_roots(g_state.app, &roots);
    ml_set_storage_roots((const char *const *)roots, root_count);
    jni_free_storage_roots(roots, root_count);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, 256 * 1024); // ML_MAX_DEPTH frames, small ones
    pthread_t tid;
    int rc = pthread_create(&tid, &attr, scan_thread_func, NULL);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        LOGI("could not start the scan thread (%d) -- falling back to a blocking scan", rc);
        atomic_store(&g_scan_running, false);
        scan_thread_func(NULL);
    }
}

static void set_music_mode_all(void);

static bool consume_scan_result(void) {
    if (!atomic_load_explicit(&g_scan_done, memory_order_acquire)) return false;
    atomic_store(&g_scan_done, false);

    // Order matters: in album mode `music_view` is an index INTO the
    // library's arena, so the view has to go before the arena it points
    // at does.
    set_music_mode_all();
    ml_str_list_free(&g_state.library);
    g_state.library = g_scan_result;
    memset(&g_scan_result, 0, sizeof(g_scan_result));
    return true;
}

// ---------------------------------------------------------------------
// MUSIC screen views
// ---------------------------------------------------------------------
static const StrList *music_list(void) {
    return g_state.musica_mode == 0 ? &g_state.library : &g_state.music_view;
}

static int music_count(void) { return music_list()->count; }

static const char *music_path_at(int i) {
    const StrList *l = music_list();
    return (i >= 0 && i < l->count) ? l->items[i] : NULL;
}

static const char *music_label_at(int i) {
    const char *p = music_path_at(i);
    if (p == NULL) return "";
    // Album names (mode 1) are already just a name; track rows show the
    // filename, which is a pointer INTO the path -- not a second copy
    // of it, which is what the old music_labels[] array was.
    return g_state.musica_mode == 1 ? p : filename_of(p);
}

static void set_music_mode_all(void) {
    ml_str_list_free(&g_state.music_view);
    g_state.musica_mode = 0;
    g_state.music_offset = 0;
    g_state.musica_selected = -1;
    g_state.musica_current_album[0] = '\0';
}

// True for folder names that are a disc marker rather than an album:
// "Disc 1", "disc_2", "CD3", "Disk 04". Deliberately strict -- it must
// match the whole name, so a real album called "Disc" or "CD Single"
// is not swallowed.
static bool is_disc_folder(const char *name, size_t len) {
    size_t i = 0;
    if (len >= 4 && strncasecmp(name, "disc", 4) == 0) i = 4;
    else if (len >= 4 && strncasecmp(name, "disk", 4) == 0) i = 4;
    else if (len >= 2 && strncasecmp(name, "cd", 2) == 0) i = 2;
    else return false;

    while (i < len && (name[i] == ' ' || name[i] == '_' || name[i] == '-' || name[i] == '.')) i++;
    if (i >= len) return false;          // "Disc" on its own is a real name
    size_t digits = 0;
    while (i < len && name[i] >= '0' && name[i] <= '9') { i++; digits++; }
    return digits > 0 && i == len;       // nothing may follow the number
}

// Album = the name of the immediate parent folder of the track file --
// works for any folder-per-album layout without needing to parse FLAC
// tags. Falls back to "(unknown)" for tracks with no parent folder.
//
// Multi-disc sets are the one layout where the immediate parent is the
// wrong answer: .../Songs Of Faith And Devotion/Disc 1/01 track.flac
// would list an album called "Disc 1", and a library with several
// box sets would show "Disc 1", "Disc 2", "Disc 3" as separate albums
// with tracks from unrelated records mixed together. When the parent is
// a disc marker the grandparent is used instead, so every disc of a set
// collapses into the one album it belongs to -- and because the master
// list is sorted by full path, the tracks still come out in disc then
// track order.
static void album_of(const char *path, char *out, size_t out_sz) {
    const char *last = strrchr(path, '/');
    if (last == NULL) {
        snprintf(out, out_sz, "(unknown)");
        return;
    }

    // start = first char of the parent folder's name
    const char *start = path;
    for (const char *p = path; p < last; p++) {
        if (*p == '/') start = p + 1;
    }
    size_t len = (size_t)(last - start);

    if (len > 0 && is_disc_folder(start, len) && start > path + 1) {
        // Walk up one level: find the grandparent's name.
        const char *parent_slash = start - 1;      // the '/' before `start`
        const char *gstart = path;
        for (const char *p = path; p < parent_slash; p++) {
            if (*p == '/') gstart = p + 1;
        }
        size_t glen = (size_t)(parent_slash - gstart);
        if (glen > 0) {
            start = gstart;
            len = glen;
        }
    }

    if (len == 0) {
        snprintf(out, out_sz, "(unknown)");
        return;
    }
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, start, len);
    out[len] = '\0';
}

static int cmp_ci_ptr(const void *a, const void *b) {
    return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

// View: unique album (folder) names from the master list, alphabetically.
static void build_album_list(void) {
    ml_str_list_free(&g_state.music_view);
    g_state.musica_mode = 1;
    g_state.music_offset = 0;
    g_state.musica_selected = -1;
    g_state.musica_current_album[0] = '\0';

    int n = g_state.library.count;
    if (n <= 0) return;

    // One arena for the names, sized as it goes. The version this
    // replaces malloc'd `music_all_count * 192` bytes up front -- a
    // 3,000-track library reserved 576KB of scratch to hold what is
    // usually a couple of hundred short strings -- and then did an
    // O(albums) linear scan per track to deduplicate. Sorting first and
    // comparing against the previous name makes it O(n log n) with no
    // oversized scratch.
    char **all = (char **)malloc(sizeof(char *) * (size_t)n);
    char *names = (char *)malloc((size_t)n * ALBUM_NAME_MAX);
    if (all == NULL || names == NULL) {
        free(all);
        free(names);
        return;
    }
    for (int i = 0; i < n; i++) {
        all[i] = names + (size_t)i * ALBUM_NAME_MAX;
        album_of(g_state.library.items[i], all[i], ALBUM_NAME_MAX);
    }
    qsort(all, (size_t)n, sizeof(char *), cmp_ci_ptr);

    // Pack the unique ones into an exactly-sized arena.
    size_t total = 0;
    int unique = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0 && strcmp(all[i], all[i - 1]) == 0) continue;
        total += strlen(all[i]) + 1;
        unique++;
    }

    char *arena = (char *)malloc(total);
    char **items = (char **)malloc(sizeof(char *) * (size_t)unique);
    if (arena == NULL || items == NULL) {
        free(arena);
        free(items);
        free(all);
        free(names);
        return;
    }

    size_t off = 0;
    int w = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0 && strcmp(all[i], all[i - 1]) == 0) continue;
        size_t len = strlen(all[i]) + 1;
        memcpy(arena + off, all[i], len);
        items[w++] = arena + off;
        off += len;
    }

    free(all);
    free(names);

    g_state.music_view.arena = arena;
    g_state.music_view.items = items;
    g_state.music_view.count = w;
    g_state.music_view.owns_arena = true;
}

// View: only the tracks belonging to one album. The paths themselves are
// NOT copied -- this is an index into the library's arena, so a
// 20-track album costs 160 bytes, not 20 strdup()s.
static void filter_music_by_album(const char *album_name) {
    char wanted[ALBUM_NAME_MAX];
    snprintf(wanted, sizeof(wanted), "%s", album_name);

    ml_str_list_free(&g_state.music_view);
    g_state.musica_mode = 2;
    g_state.music_offset = 0;
    g_state.musica_selected = -1;
    snprintf(g_state.musica_current_album, sizeof(g_state.musica_current_album), "%s", wanted);

    int matches = 0;
    char alb[ALBUM_NAME_MAX];
    for (int i = 0; i < g_state.library.count; i++) {
        album_of(g_state.library.items[i], alb, sizeof(alb));
        if (strcmp(alb, wanted) == 0) matches++;
    }
    if (matches == 0) return;

    char **items = (char **)malloc(sizeof(char *) * (size_t)matches);
    if (items == NULL) return;

    int w = 0;
    for (int i = 0; i < g_state.library.count && w < matches; i++) {
        album_of(g_state.library.items[i], alb, sizeof(alb));
        if (strcmp(alb, wanted) == 0) items[w++] = g_state.library.items[i];
    }

    g_state.music_view.arena = NULL;
    g_state.music_view.items = items;
    g_state.music_view.count = w;
    g_state.music_view.owns_arena = false; // text belongs to `library`
}

static void load_music_library(void) {
    set_music_mode_all();
    ml_str_list_free(&g_state.library);
    if (!ml_load_cache(&g_state.library)) {
        LOGI("cache empty -- scanning storage in the background");
        start_scan_if_idle();
    }
    set_music_mode_all();
}

// ---------------------------------------------------------------------
// PLAYLISTS screen rows
// ---------------------------------------------------------------------
static int row_display_count(void) {
    return g_state.rows.count + (g_state.rows_have_create ? 1 : 0);
}

// Returns NULL for the synthetic "+ NEW PLAYLIST" row.
static const char *row_entry_at(int i) {
    if (g_state.rows_have_create) {
        if (i == 0) return NULL;
        i--;
    }
    return (i >= 0 && i < g_state.rows.count) ? g_state.rows.items[i] : NULL;
}

static const char *row_label_at(int i) {
    const char *e = row_entry_at(i);
    if (e == NULL) return "+ NEW PLAYLIST";
    return g_state.rows_are_tracks ? filename_of(e) : e;
}

static void load_playlist_names_into_rows(int prepend_create_row) {
    ml_str_list_free(&g_state.rows);
    ml_list_playlists(&g_state.rows);
    g_state.rows_are_tracks = 0;
    g_state.rows_have_create = prepend_create_row ? 1 : 0;
    g_state.row_offset = 0;
}

#define DP(d) ((int)((d) * g_state.density + 0.5f))

#define MIN_TOUCH_H DP(48)
#define LONG_PRESS_MS 450LL

#define BP_REASON_SHOW_MS 3000LL

static int top_bar_h(int scale) {
    int text_based = ui_glyph_h(scale) + DP(16);
    return text_based > MIN_TOUCH_H ? text_based : MIN_TOUCH_H;
}
static int row_h(int scale) {
    int text_based = ui_glyph_h(scale) + DP(14);
    return text_based > MIN_TOUCH_H ? text_based : MIN_TOUCH_H;
}
#define TOP_BAR_H(scale) top_bar_h(scale)
#define ROW_H(scale) row_h(scale)

static int bottom_reserved_h(void) {
    int reserved = ROW_H(g_state.font_scale);
    // The status line ("Added to X", "Playlist deleted", "DAC volume:
    // hardware") is drawn one row above the bottom nav, in space the
    // list would otherwise scroll into -- so it used to overlap the last
    // visible row. Reserve that row while a message is up.
    if (g_state.status_msg[0] != '\0') {
        reserved += ROW_H(g_state.font_scale);
    }
    if (g_state.screen == SCREEN_MUSICA && !g_state.musica_picking_playlist &&
        g_state.musica_selected >= 0) {
        reserved += ROW_H(g_state.font_scale);
    }
    if (g_state.screen == SCREEN_PLAYLISTS && g_state.playlists_viewing) {
        reserved += ROW_H(g_state.font_scale);
    }
    return reserved;
}

static int musica_filter_bar_h(void) {
    if (g_state.screen == SCREEN_MUSICA && !g_state.musica_picking_playlist) {
        return ROW_H(g_state.font_scale);
    }
    return 0;
}

static int visible_rows(void) {
    int usable = g_state.height - TOP_BAR_H(g_state.font_scale) - bottom_reserved_h() - musica_filter_bar_h();
    int rh = ROW_H(g_state.font_scale);
    return rh > 0 ? usable / rh : 0;
}

static void draw_columns(ui_pixel_t *px, ui_pixel_t fg, ui_pixel_t bg, int y, int h,
                          const char *const *labels, int n, int highlight_idx) {
    if (n <= 0) return;
    int col_w = g_state.width / n;
    int glyph_h = ui_glyph_h(g_state.font_scale);
    int ty = y + (h - glyph_h) / 2;
    for (int i = 0; i < n; i++) {
        int col_x = i * col_w;
        int text_w = ui_text_width(labels[i], g_state.font_scale);
        int tx = col_x + (col_w - text_w) / 2;
        if (tx < col_x) tx = col_x;
        if (i == highlight_idx) {
            ui_fill_rect(px, g_state.stride, col_x, y, col_w, h, fg);
            ui_draw_text_clipped(px, g_state.stride, tx, ty, labels[i], g_state.font_scale, bg,
                                 col_x + col_w - tx);
        } else {
            ui_draw_text_clipped(px, g_state.stride, tx, ty, labels[i], g_state.font_scale, fg,
                                 col_x + col_w - tx);
        }
        if (i < n - 1) {
            ui_fill_rect(px, g_state.stride, col_x + col_w - 1, y, 1, h, fg);
        }
    }
}

static int column_hit(float x, int n) {
    if (n <= 0) return 0;
    int col_w = g_state.width / n;
    if (col_w <= 0) return 0;
    int idx = (int)(x / col_w);
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return idx;
}

static void draw_top_nav(ui_pixel_t *px, ui_pixel_t fg, ui_pixel_t bg) {
    const char *labels[3] = {"MUSIC", "PLAYLISTS", "CONFIG"};
    int bar_h = TOP_BAR_H(g_state.font_scale);
    int sel = g_state.screen == SCREEN_MUSICA ? 0
              : g_state.screen == SCREEN_PLAYLISTS ? 1
              : g_state.screen == SCREEN_CONFIG ? 2 : -1;
    draw_columns(px, fg, bg, 0, bar_h, labels, 3, sel);
}

// Row directly under the top nav bar, MUSIC screen only: switches between
// the flat "every track" view and browsing by album (see musica_mode).
static void draw_musica_filter_bar(ui_pixel_t *px, ui_pixel_t fg, ui_pixel_t bg) {
    if (g_state.musica_picking_playlist) return;
    int rh = ROW_H(g_state.font_scale);
    int y = TOP_BAR_H(g_state.font_scale);
    if (g_state.musica_mode == 2) {
        char label_buf[224];
        snprintf(label_buf, sizeof(label_buf), "< ALBUMS (%s)", g_state.musica_current_album);
        const char *labels[1] = {label_buf};
        draw_columns(px, fg, bg, y, rh, labels, 1, -1);
    } else {
        const char *labels[2] = {"ALL TRACKS", "ALBUMS"};
        draw_columns(px, fg, bg, y, rh, labels, 2, g_state.musica_mode);
    }
}

static void handle_tap_musica_filter_bar(float x) {
    if (g_state.musica_mode == 2) {
        build_album_list();
        return;
    }
    int col = column_hit(x, 2);
    if (col == 0) {
        set_music_mode_all();
    } else {
        build_album_list();
    }
}

static void draw_bottom_playernav(ui_pixel_t *px, ui_pixel_t fg, ui_pixel_t bg) {
    int rh = ROW_H(g_state.font_scale);
    int y = g_state.height - rh;
    if (g_state.screen == SCREEN_PLAYER) {
        const char *labels[1] = {"<"};
        draw_columns(px, fg, bg, y, rh, labels, 1, -1);
        return;
    }
    const char *labels[1] = {g_state.snap.current_path[0] != '\0' ? ">" : "NOTHING PLAYING"};
    draw_columns(px, fg, bg, y, rh, labels, 1, -1);
}

static void handle_tap_bottom_playernav(void) {
    if (g_state.screen == SCREEN_PLAYER) {
        g_state.screen = g_state.player_return_screen;
        return;
    }
    if (g_state.snap.current_path[0] == '\0') return;
    g_state.player_return_screen = g_state.screen;
    g_state.screen = SCREEN_PLAYER;
}

static void draw_wrapped_text(ui_pixel_t *px, ui_pixel_t fg, int x, int y, const char *text, int scale) {
    int gw = ui_glyph_w(scale);
    int rh_line = ui_glyph_h(scale) + 4;
    int max_chars = gw > 0 ? (g_state.width - x - 8) / gw : 40;
    if (max_chars < 10) max_chars = 10;
    if (max_chars > 250) max_chars = 250;

    char buf[512];
    snprintf(buf, sizeof(buf), "%s", text);

    char line[256];
    line[0] = '\0';
    int line_len = 0;
    int cy = y;

    char *save = NULL;
    char *word = strtok_r(buf, " ", &save);
    while (word != NULL) {
        int word_len = (int)strlen(word);
        int needed = line_len == 0 ? word_len : line_len + 1 + word_len;
        if (needed > max_chars && line_len > 0) {
            ui_draw_text(px, g_state.stride, x, cy, line, scale, fg);
            cy += rh_line;
            line[0] = '\0';
            line_len = 0;
        }
        if (line_len > 0) {
            strncat(line, " ", sizeof(line) - line_len - 1);
            line_len++;
        }
        strncat(line, word, sizeof(line) - line_len - 1);
        line_len += word_len;
        word = strtok_r(NULL, " ", &save);
    }
    if (line_len > 0) {
        ui_draw_text(px, g_state.stride, x, cy, line, scale, fg);
    }
}

static void draw_list_rows(ui_pixel_t *px, ui_pixel_t fg, ui_pixel_t bg, int y0, int rh, int vis,
                            int offset, int count, const char *(*label_at)(int), int selected) {
    int scale = g_state.font_scale;
    int gh = ui_glyph_h(scale);
    for (int i = 0; i < vis && (offset + i) < count; i++) {
        int idx = offset + i;
        int row_y = y0 + i * rh;
        int ty = row_y + (rh - gh) / 2;
        if (idx == selected) {
            ui_fill_rect(px, g_state.stride, 0, row_y, g_state.width, rh, fg);
            ui_draw_text_clipped(px, g_state.stride, 8, ty, label_at(idx), scale, bg, g_state.width - 16);
        } else {
            ui_draw_text_clipped(px, g_state.stride, 8, ty, label_at(idx), scale, fg, g_state.width - 16);
        }
    }
}

static const char *empty_music_message(void) {
    if (atomic_load(&g_scan_running)) return "(scanning storage for .flac files...)";
    // Distinguish "you have no FLAC files" from "you did not let me look",
    // which are very different problems with very different fixes.
    if (!native_bridge_has_storage_permission()) {
        return "(no permission to read audio -- allow it in\nAndroid Settings > Apps > Flast > Permissions)";
    }
    return "(empty -- no .flac files found)";
}

static void draw_screen_musica(ui_pixel_t *px, ui_pixel_t fg, ui_pixel_t bg) {
    int scale = g_state.font_scale;
    int rh = ROW_H(scale);
    int y0 = TOP_BAR_H(scale) + musica_filter_bar_h();
    int vis = visible_rows();

    if (g_state.musica_picking_playlist) {
        draw_list_rows(px, fg, bg, y0, rh, vis, g_state.row_offset, row_display_count(),
                       row_label_at, -1);
        if (row_display_count() == 0) {
            ui_draw_text(px, g_state.stride, 8, y0, "(no playlists yet)", scale, fg);
        }
        return;
    }

    draw_list_rows(px, fg, bg, y0, rh, vis, g_state.music_offset, music_count(),
                   music_label_at, g_state.musica_selected);
    if (music_count() == 0) {
        ui_draw_text(px, g_state.stride, 8, y0, empty_music_message(), scale, fg);
    }

    if (g_state.musica_selected >= 0) {
        const char *labels[2] = {"PLAY", "ADD TO PLAYLIST"};
        draw_columns(px, fg, bg, g_state.height - 2 * rh, rh, labels, 2, -1);
    } else if (g_state.status_msg[0] != '\0') {
        ui_draw_text(px, g_state.stride, 8, g_state.height - 2 * rh, g_state.status_msg, scale, fg);
    }
}

static void draw_screen_playlists(ui_pixel_t *px, ui_pixel_t fg, ui_pixel_t bg) {
    int scale = g_state.font_scale;
    int rh = ROW_H(scale);
    int y0 = TOP_BAR_H(scale);
    int vis = visible_rows();

    draw_list_rows(px, fg, bg, y0, rh, vis, g_state.row_offset, row_display_count(),
                   row_label_at, -1);
    if (row_display_count() == 0) {
        ui_draw_text(px, g_state.stride, 8, y0,
                     g_state.playlists_viewing ? "(empty playlist)" : "(no playlists)", scale, fg);
    }

    if (g_state.playlists_viewing) {
        char label_buf[160];
        snprintf(label_buf, sizeof(label_buf), "DELETE PLAYLIST: %s", g_state.playlists_viewed_name);
        const char *labels[1] = {label_buf};
        draw_columns(px, fg, bg, g_state.height - 2 * rh, rh, labels, 1, -1);
    } else if (g_state.status_msg[0] != '\0') {
        ui_draw_text(px, g_state.stride, 8, g_state.height - 2 * rh, g_state.status_msg, scale, fg);
    }
}

static void draw_screen_config(ui_pixel_t *px, ui_pixel_t fg, ui_pixel_t bg) {
    int scale = g_state.font_scale;
    int rh = ROW_H(scale);
    int y = TOP_BAR_H(scale);
    int gh = ui_glyph_h(scale);

    char line[96];
    snprintf(line, sizeof(line), "FONT SIZE: %d", g_state.font_scale);
    ui_draw_text(px, g_state.stride, 8, y + (rh - gh) / 2, line, scale, fg);
    y += rh;

    const char *fs_labels[2] = {"-", "+"};
    draw_columns(px, fg, bg, y, rh, fs_labels, 2, -1);
    y += rh;

    snprintf(line, sizeof(line), "THEME: %s", g_state.theme_black ? "BLACK" : "WHITE");
    ui_draw_text(px, g_state.stride, 8, y + (rh - gh) / 2, line, scale, fg);
    y += rh;

    const char *theme_labels[2] = {"BLACK", "WHITE"};
    draw_columns(px, fg, bg, y, rh, theme_labels, 2, g_state.theme_black ? 0 : 1);
    y += rh + 8;

    int bp_state = jni_get_bit_perfect_state(g_state.app, g_state.snap.is_bit_perfect_native);
    const char *bp_label = bp_state == 0 ? "BIT-PERFECT: YES"
                            : bp_state == 1 ? "BIT-PERFECT: PARTIAL" : "BIT-PERFECT: NO";
    ui_draw_text(px, g_state.stride, 8, y, bp_label, scale, fg);
    y += gh + 6;

    char *reason = jni_get_bit_perfect_reason(g_state.app, g_state.snap.is_bit_perfect_native);
    if (reason != NULL) {
        draw_wrapped_text(px, fg, 8, y, reason, scale);
        free(reason);
    }
}

static const char *bit_perfect_label(const PlaybackSnapshot *s) {
    int state = jni_get_bit_perfect_state(g_state.app, s->is_bit_perfect_native);
    if (state == 0) return "BIT-PERFECT: YES";
    if (state == 1) return "BIT-PERFECT: PARTIAL";
    return "BIT-PERFECT: NO";
}

static void draw_screen_player(ui_pixel_t *px, ui_pixel_t fg, ui_pixel_t bg) {
    const PlaybackSnapshot *s = &g_state.snap;
    int scale = g_state.font_scale;
    int rh = ROW_H(scale);

    int y = TOP_BAR_H(scale);

    const char *name = (s->has_pending_nav && s->pending_preview_path[0] != '\0')
                            ? filename_of(s->pending_preview_path)
                            : (s->current_path[0] ? filename_of(s->current_path) : "(nothing queued)");
    ui_draw_text(px, g_state.stride, 8, y, name, scale, fg);
    y += rh * 2;

    if (g_state.bp_reason_hide_at_ms > now_ms() && g_state.bp_reason_msg[0] != '\0') {
        draw_wrapped_text(px, fg, 8, y, g_state.bp_reason_msg, scale);
    } else {
        ui_draw_text(px, g_state.stride, 8, y, bit_perfect_label(s), scale, fg);
    }
    y += rh * 2;

    char pos[64];
    snprintf(pos, sizeof(pos), "%02d:%02d", (int)(s->position_seconds / 60), (int)s->position_seconds % 60);
    ui_draw_text(px, g_state.stride, 8, y, pos, scale, fg);
    y += rh * 2;

    const char *play_label = s->is_paused ? "PLAY" : (s->is_playing ? "PAUSE" : "PLAY");
    const char *labels[3] = {"< PREV", play_label, "NEXT >"};
    draw_columns(px, fg, bg, y, rh, labels, 3, -1);
}

// The exact question from spec 3.5: two lines of context, then the
// question, then two buttons. Not a tutorial and not onboarding -- the
// minimum explanation for why playback just stopped, for a user who has
// never read the design document.
static const char *const k_dac_lines[] = {
    "NEW USB DAC DETECTED",
    "",
    "To protect your hearing and keep",
    "bit-perfect playback, we need to",
    "know this once for each DAC:",
    "",
    "Does your DAC let you change the",
    "volume directly (a wheel, physical",
    "buttons, or its own app)?",
};
#define DAC_LINE_COUNT ((int)(sizeof(k_dac_lines) / sizeof(k_dac_lines[0])))


// Layout is computed once here and consumed by both the drawing and the
// tap handler, so a button can never be drawn somewhere different from
// where it is hit-tested -- which on this particular screen would mean a
// hearing-safety question that cannot be answered.
typedef struct {
    int scale;
    int line_h;
    int text_y;
    int buttons_y;
    int buttons_h;
    int margin;
} DacLayout;

static DacLayout dac_layout(void) {
    DacLayout L;
    int longest = 0;
    for (int i = 0; i < DAC_LINE_COUNT; i++) {
        int len = (int)strlen(k_dac_lines[i]);
        if (len > longest) longest = len;
    }
    L.margin = DP(16);
    // Largest font scale whose longest line still fits. Chosen per-draw
    // rather than using g_state.font_scale directly, because the user can
    // set the font to 4 in CONFIG and this text must never be clipped --
    // it is the one screen where an unreadable word could mean the wrong
    // answer to a hearing-safety question.
    L.scale = g_state.font_scale;
    while (L.scale > 1 && longest * ui_glyph_w(L.scale) > g_state.width - 2 * L.margin) L.scale--;

    L.line_h = ui_glyph_h(L.scale) + DP(6);
    L.buttons_h = ROW_H(L.scale);

    int gap = DP(40);
    int content_h = DAC_LINE_COUNT * L.line_h + gap + L.buttons_h;
    L.text_y = (g_state.height - content_h) / 2;
    if (L.text_y < DP(24)) L.text_y = DP(24);
    L.buttons_y = L.text_y + DAC_LINE_COUNT * L.line_h + gap;
    return L;
}

static void draw_screen_dac_question(ui_pixel_t *px, ui_pixel_t fg) {
    DacLayout L = dac_layout();

    int y = L.text_y;
    for (int i = 0; i < DAC_LINE_COUNT; i++) {
        if (k_dac_lines[i][0] != '\0') {
            ui_draw_text_clipped(px, g_state.stride, L.margin, y, k_dac_lines[i], L.scale, fg,
                                 g_state.width - 2 * L.margin);
        }
        y += L.line_h;
    }

    // Two outlined boxes rather than bare centred words. On the previous
    // pass these were drawn with the generic column helper and read as
    // labels, not as things to press -- which is the wrong affordance for
    // the only control on a modal screen that is blocking playback.
    const char *labels[2] = {"YES", "NO"};
    int half = g_state.width / 2;
    int box_w = half - 2 * L.margin;
    int border = DP(2) < 1 ? 1 : DP(2);
    int gh = ui_glyph_h(L.scale);
    for (int i = 0; i < 2; i++) {
        int bx = i * half + L.margin;
        ui_fill_rect(px, g_state.stride, bx, L.buttons_y, box_w, border, fg);
        ui_fill_rect(px, g_state.stride, bx, L.buttons_y + L.buttons_h - border, box_w, border, fg);
        ui_fill_rect(px, g_state.stride, bx, L.buttons_y, border, L.buttons_h, fg);
        ui_fill_rect(px, g_state.stride, bx + box_w - border, L.buttons_y, border, L.buttons_h, fg);

        int tw = ui_text_width(labels[i], L.scale);
        ui_draw_text(px, g_state.stride, bx + (box_w - tw) / 2,
                     L.buttons_y + (L.buttons_h - gh) / 2, labels[i], L.scale, fg);
    }
}

static void handle_tap_dac_question(float x, float y) {
    DacLayout L = dac_layout();
    if (y < L.buttons_y || y >= L.buttons_y + L.buttons_h) return; // modal: rest is inert
    bool has_own_control = (x < (float)(g_state.width / 2));
    native_bridge_answer_dac_question(g_state.app, has_own_control);
    g_state.screen = g_state.dac_return_screen;
    snprintf(g_state.status_msg, sizeof(g_state.status_msg),
             has_own_control ? "DAC volume: hardware" : "DAC volume: system software");
}

static void draw_frame(void) {
    if (g_state.window == NULL) return;
    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(g_state.window, &buf, NULL) != 0) return;

    // RGB_565 is a CDD-mandated gralloc format and setBuffersGeometry()
    // is the documented way to ask for it, so this should never fire --
    // but if some ROM ever hands back a different geometry, the logcat
    // says so in one line instead of leaving a garbled screen to guess
    // at. (buf.format uses the same ANativeWindow legacy format values
    // as UI_WINDOW_FORMAT.)
    static int s_format_checked = 0;
    if (!s_format_checked) {
        s_format_checked = 1;
        if (buf.format != UI_WINDOW_FORMAT) {
            LOGI("WARNING: window format is %d, expected %d (RGB_565) -- see ui_render.h",
                 buf.format, UI_WINDOW_FORMAT);
        }
    }

    g_state.width = buf.width;
    g_state.height = buf.height;
    g_state.stride = buf.stride;
    ui_render_begin(buf.width, buf.height, buf.stride);

    pc_get_snapshot(&g_state.snap);

    ui_pixel_t bg = g_state.theme_black ? UI_COLOR_BLACK : UI_COLOR_WHITE;
    ui_pixel_t fg = g_state.theme_black ? UI_COLOR_WHITE : UI_COLOR_BLACK;
    ui_pixel_t *px = (ui_pixel_t *)buf.bits;
    ui_fill_rect(px, buf.stride, 0, 0, buf.width, buf.height, bg);

    if (g_state.screen == SCREEN_DAC_QUESTION) {
        // Modal: no nav chrome at all, so there is nothing to tap that
        // could leave the question unanswered while audio is blocked.
        draw_screen_dac_question(px, fg);
        ANativeWindow_unlockAndPost(g_state.window);
        return;
    }

    if (g_state.screen != SCREEN_PLAYER) {
        draw_top_nav(px, fg, bg);
    }
    if (g_state.screen == SCREEN_MUSICA) {
        draw_musica_filter_bar(px, fg, bg);
    }

    switch (g_state.screen) {
        case SCREEN_MUSICA:
            draw_screen_musica(px, fg, bg);
            break;
        case SCREEN_PLAYLISTS:
            draw_screen_playlists(px, fg, bg);
            break;
        case SCREEN_CONFIG:
            draw_screen_config(px, fg, bg);
            break;
        case SCREEN_PLAYER:
            draw_screen_player(px, fg, bg);
            break;
        case SCREEN_DAC_QUESTION:
            break; // handled above, before any chrome is drawn
    }

    draw_bottom_playernav(px, fg, bg);

    ANativeWindow_unlockAndPost(g_state.window);
}

static void apply_scroll(float dy) {
    int rh = ROW_H(g_state.font_scale);
    if (rh <= 0) return;
    int rows_delta = (int)(dy / rh);
    if (rows_delta == 0) return;

    int *offset;
    int count;
    if (g_state.screen == SCREEN_MUSICA && !g_state.musica_picking_playlist) {
        offset = &g_state.music_offset;
        count = music_count();
    } else if (g_state.screen == SCREEN_MUSICA || g_state.screen == SCREEN_PLAYLISTS) {
        offset = &g_state.row_offset;
        count = row_display_count();
    } else {
        return;
    }

    int visible = visible_rows();
    int max_offset = count - visible;
    if (max_offset < 0) max_offset = 0;
    *offset += rows_delta;
    if (*offset < 0) *offset = 0;
    if (*offset > max_offset) *offset = max_offset;
    g_state.ui_dirty = 1;
}

static void handle_tap_player(float x, float y) {
    int scale = g_state.font_scale;
    int rh = ROW_H(scale);
    int y0 = TOP_BAR_H(scale);
    int y_bp = y0 + rh * 2;
    int y_controls = y0 + rh * 6;

    if (y >= y_bp && y < y_bp + rh) {
        char *reason = jni_get_bit_perfect_reason(g_state.app, g_state.snap.is_bit_perfect_native);
        if (reason != NULL) {
            snprintf(g_state.bp_reason_msg, sizeof(g_state.bp_reason_msg), "%s", reason);
            free(reason);
        } else {
            g_state.bp_reason_msg[0] = '\0';
        }
        g_state.bp_reason_hide_at_ms = now_ms() + BP_REASON_SHOW_MS;
        return;
    }
    if (y >= y_controls && y < y_controls + rh) {
        int col = column_hit(x, 3);
        if (col == 0) pc_request_prev();
        else if (col == 1) pc_toggle_play_pause();
        else pc_request_next();
        return;
    }
}

// Builds the playback queue as its own packed arena copy and hands
// ownership to playback_controller. One allocation for the text plus one
// for the index, instead of the one-strdup()-per-track the old
// dup_paths_for_queue() did (3,000 tracks meant 3,000 malloc headers).
static bool build_queue(const StrList *src, StrList *out) {
    memset(out, 0, sizeof(*out));
    if (src->count <= 0) return false;

    size_t total = 0;
    for (int i = 0; i < src->count; i++) total += strlen(src->items[i]) + 1;

    char *arena = (char *)malloc(total);
    char **items = (char **)malloc(sizeof(char *) * (size_t)src->count);
    if (arena == NULL || items == NULL) {
        free(arena);
        free(items);
        return false;
    }

    size_t off = 0;
    for (int i = 0; i < src->count; i++) {
        size_t len = strlen(src->items[i]) + 1;
        memcpy(arena + off, src->items[i], len);
        items[i] = arena + off;
        off += len;
    }

    out->arena = arena;
    out->items = items;
    out->count = src->count;
    out->owns_arena = true;
    return true;
}

static void play_music_row(int index) {
    StrList queue;
    if (!build_queue(music_list(), &queue)) return;
    pc_set_queue(&queue, index);
    pc_play_index(index);
    g_state.musica_selected = -1;
    g_state.player_return_screen = g_state.screen;
    g_state.screen = SCREEN_PLAYER;
}

static void handle_tap_musica(float x, float y) {
    int scale = g_state.font_scale;
    int rh = ROW_H(scale);
    int y0 = TOP_BAR_H(scale);

    if (g_state.musica_picking_playlist) {
        if (y < y0) return;
        int row = (int)((y - y0) / rh) + g_state.row_offset;
        if (row < 0 || row >= row_display_count()) return;

        char target_name[128];
        const char *entry = row_entry_at(row);
        if (entry == NULL) {
            if (!ml_create_auto_playlist(target_name, sizeof(target_name))) return;
        } else {
            snprintf(target_name, sizeof(target_name), "%s", entry);
        }
        const char *selected_path = music_path_at(g_state.musica_selected);
        if (selected_path != NULL) {
            ml_add_to_playlist(target_name, selected_path);
            snprintf(g_state.status_msg, sizeof(g_state.status_msg), "Added to %s", target_name);
        }
        g_state.musica_picking_playlist = 0;
        g_state.musica_selected = -1;
        return;
    }

    int fbh = musica_filter_bar_h();
    if (fbh > 0 && y >= y0 && y < y0 + fbh) {
        handle_tap_musica_filter_bar(x);
        return;
    }
    y0 += fbh;

    if (g_state.musica_selected >= 0 && y >= g_state.height - 2 * rh && y < g_state.height - rh) {
        int col = column_hit(x, 2);
        if (col == 0) {
            play_music_row(g_state.musica_selected);
        } else {
            g_state.musica_picking_playlist = 1;
            load_playlist_names_into_rows(1);
        }
        return;
    }

    if (y < y0) return;
    int row = (int)((y - y0) / rh) + g_state.music_offset;
    if (row < 0 || row >= music_count()) return;

    if (g_state.musica_mode == 1) {
        // Copy the album name out before filtering: filter_music_by_album()
        // frees the very list this pointer points into.
        char album[ALBUM_NAME_MAX];
        snprintf(album, sizeof(album), "%s", music_path_at(row));
        filter_music_by_album(album);
        return;
    }
    play_music_row(row);
}

static void handle_long_press_musica(float x, float y) {
    (void)x;
    if (g_state.musica_picking_playlist) return;
    if (g_state.musica_mode == 1) return; // rows are album names, not tracks
    int scale = g_state.font_scale;
    int rh = ROW_H(scale);
    int y0 = TOP_BAR_H(scale) + musica_filter_bar_h();
    if (y < y0) return;
    if (y >= g_state.height - rh) return; // always-present bottom nav row
    if (g_state.musica_selected >= 0 && y >= g_state.height - 2 * rh) return;
    int row = (int)((y - y0) / rh) + g_state.music_offset;
    if (row < 0 || row >= music_count()) return;
    g_state.musica_selected = row;
    g_state.ui_dirty = 1;
}

static void handle_tap_playlists(float x, float y) {
    (void)x;
    int scale = g_state.font_scale;
    int rh = ROW_H(scale);
    int y0 = TOP_BAR_H(scale);

    if (g_state.playlists_viewing) {
        if (y >= g_state.height - 2 * rh && y < g_state.height - rh) {
            ml_delete_playlist(g_state.playlists_viewed_name);
            snprintf(g_state.status_msg, sizeof(g_state.status_msg), "Playlist deleted");
            g_state.playlists_viewing = 0;
            load_playlist_names_into_rows(1);
            return;
        }
        if (y < y0) return;
        int row = (int)((y - y0) / rh) + g_state.row_offset;
        if (row < 0 || row >= row_display_count()) return;
        StrList queue;
        if (!build_queue(&g_state.rows, &queue)) return;
        pc_set_queue(&queue, row);
        pc_play_index(row);
        g_state.player_return_screen = SCREEN_PLAYLISTS;
        g_state.screen = SCREEN_PLAYER;
        return;
    }

    if (y < y0) return;
    int row = (int)((y - y0) / rh) + g_state.row_offset;
    if (row < 0 || row >= row_display_count()) return;

    const char *entry = row_entry_at(row);
    if (entry == NULL) {
        char created[128];
        if (ml_create_auto_playlist(created, sizeof(created))) {
            snprintf(g_state.status_msg, sizeof(g_state.status_msg), "Created: %s", created);
        }
        load_playlist_names_into_rows(1);
        return;
    }

    snprintf(g_state.playlists_viewed_name, sizeof(g_state.playlists_viewed_name), "%s", entry);
    ml_str_list_free(&g_state.rows);
    ml_load_playlist(g_state.playlists_viewed_name, &g_state.rows);
    g_state.rows_are_tracks = 1;
    g_state.rows_have_create = 0;
    g_state.row_offset = 0;
    g_state.playlists_viewing = 1;
}

static void handle_tap_config(float x, float y) {
    int scale = g_state.font_scale;
    int rh = ROW_H(scale);
    int y0 = TOP_BAR_H(scale);

    int yB = y0 + rh;
    if (y >= yB && y < yB + rh) {
        int col = column_hit(x, 2);
        if (col == 0) { if (g_state.font_scale > 1) g_state.font_scale--; }
        else { if (g_state.font_scale < 4) g_state.font_scale++; }
        return;
    }
    int yD = y0 + rh * 3;
    if (y >= yD && y < yD + rh) {
        int col = column_hit(x, 2);
        g_state.theme_black = (col == 0) ? 1 : 0;
        return;
    }
}

static void handle_tap(float x, float y) {

    g_state.ui_dirty = 1;

    if (g_state.screen == SCREEN_DAC_QUESTION) {
        handle_tap_dac_question(x, y);
        return;
    }

    g_state.status_msg[0] = '\0';

    int bar_h = TOP_BAR_H(g_state.font_scale);
    if (g_state.screen != SCREEN_PLAYER && y < bar_h) {
        int col = column_hit(x, 3);
        if (col == 0) {
            g_state.screen = SCREEN_MUSICA;
            g_state.musica_picking_playlist = 0;
            g_state.musica_selected = -1;
        } else if (col == 1) {
            g_state.screen = SCREEN_PLAYLISTS;
            g_state.playlists_viewing = 0;
            load_playlist_names_into_rows(1);
        } else {
            g_state.screen = SCREEN_CONFIG;
        }
        return;
    }

    // Always-present bottom row ("<" on PLAYER, ">"/"NOTHING PLAYING"
    // elsewhere, see draw_bottom_playernav) -- must be checked before the
    // per-screen handlers below, since it overlaps their own row space.
    int rh = ROW_H(g_state.font_scale);
    if (y >= g_state.height - rh) {
        handle_tap_bottom_playernav();
        return;
    }

    switch (g_state.screen) {
        case SCREEN_PLAYER: handle_tap_player(x, y); break;
        case SCREEN_CONFIG: handle_tap_config(x, y); break;
        case SCREEN_MUSICA: handle_tap_musica(x, y); break;
        case SCREEN_PLAYLISTS: handle_tap_playlists(x, y); break;
        case SCREEN_DAC_QUESTION: break; // handled above
    }
}

static void handle_long_press(float x, float y) {
    if (g_state.screen == SCREEN_DAC_QUESTION) return;
    g_state.ui_dirty = 1;
    g_state.status_msg[0] = '\0';
    if (g_state.screen == SCREEN_MUSICA) {
        handle_long_press_musica(x, y);
    }

}

static int list_scroll_context(void) {
    return g_state.screen == SCREEN_MUSICA || g_state.screen == SCREEN_PLAYLISTS;
}

static int32_t on_input(struct android_app *app, AInputEvent *event) {
    (void)app;
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
    int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    float x = AMotionEvent_getX(event, 0);
    float y = AMotionEvent_getY(event, 0);

    if (action == AMOTION_EVENT_ACTION_DOWN) {
        g_state.touch_down_active = 1;
        g_state.touch_down_y = y;
        g_state.touch_down_time_ms = now_ms();
    } else if (action == AMOTION_EVENT_ACTION_UP && g_state.touch_down_active) {
        g_state.touch_down_active = 0;
        float dy = y - g_state.touch_down_y;
        if (fabsf(dy) >= (float)DP(16)) {

            if (list_scroll_context()) apply_scroll(-dy);
        } else if (now_ms() - g_state.touch_down_time_ms >= LONG_PRESS_MS) {
            handle_long_press(x, y);
        } else {
            handle_tap(x, y);
        }
    }
    return 1;
}

static void refresh_density(struct android_app *app) {
    int32_t density_dpi = AConfiguration_getDensity(app->config);
    g_state.density = (density_dpi > 0 && density_dpi < 1000) ? (float)density_dpi / 160.0f : 2.5f;
}

static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            g_state.window = app->window;
            // 16 bits per pixel, not 32. This UI draws only opaque black
            // and opaque white, both of which are exactly representable
            // in RGB_565, so the surface is visually identical while the
            // graphics buffers behind it -- the single biggest block of
            // memory this process owns -- cost half as much. See the
            // long note at the top of ui_render.h.
            ANativeWindow_setBuffersGeometry(g_state.window, 0, 0, UI_WINDOW_FORMAT);
            g_state.ui_dirty = 1;
            draw_frame();
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            // The Activity no longer gets destroyed for rotation, font
            // scale, dark mode, folding, etc (see the configChanges list
            // in AndroidManifest.xml), so this is where those land now.
            // Density can genuinely change -- an app moved to an external
            // display or a foldable's inner screen -- and every touch
            // target in this UI is sized from it.
            refresh_density(app);
            g_state.ui_dirty = 1;
            break;
        case APP_CMD_TERM_WINDOW:
            // Only the rendering surface goes away here (e.g. user swiped
            // the app away, or it's being recreated for a config change).
            // Playback must keep running in the background via
            // PlaybackService -- do NOT touch pc_* state here.
            g_state.window = NULL;
            break;
        case APP_CMD_DESTROY:
            // The native Activity/thread is going away, but the process
            // (and the foreground PlaybackService) may well keep living
            // in the background. Do not stop playback here -- that used
            // to silence the music on every back/swipe close. Playback
            // is only ever stopped explicitly (user pressed stop, DAC
            // safety cutoff, track queue exhausted, etc).
            break;
        default:
            break;
    }
}

// android_main() re-runs from scratch on a brand-new native thread every
// time the Activity is (re)created -- including after the user swipes
// the app away and taps it open again, while the process (and the
// foreground PlaybackService + any in-flight playback) is still alive.
// g_process_ever_initialized distinguishes that from a true cold start
// of the process, so pc_init() -- which wipes the queue/current track --
// only ever runs once per process lifetime, not once per reopen.
static bool g_process_ever_initialized = false;

// Which screen the user was last on. Deliberately OUTSIDE AppState,
// because AppState is wiped every time android_main() restarts -- and
// android_main() restarts on every back-press and swipe-away, even
// though the process (and playback) keeps running. Without this, coming
// back to a still-playing app dumped you on MUSIC instead of where you
// left off. A full kill from the recents list does reset it, which is
// the right behaviour for a genuinely cold start.
static Screen g_last_screen = SCREEN_MUSICA;

static void init_music_library_paths(struct android_app *app) {
    const char *dir = app->activity->internalDataPath;
    if (dir != NULL && dir[0] != '\0') {
        ml_init(dir);
        return;
    }
    // ANativeActivity::internalDataPath has historically come back NULL
    // on some devices/ROMs. Rather than lose the scan cache and every
    // playlist on those, ask the framework directly -- one JNI call,
    // once, only on the path where the cheap answer failed.
    char fallback[1024];
    if (jni_get_files_dir(app, fallback, sizeof(fallback))) {
        ml_init(fallback);
    } else {
        ml_init(NULL);
    }
}

void android_main(struct android_app *app) {
    // Fresh thread: any JNIEnv* cached from a previous (now-dead) thread
    // must be discarded before we make any JNI call on this one.
    native_bridge_reset_env();

    // Deliberately not memset(): the library and view lists may already
    // hold memory from a previous android_main() run on this process
    // (the user swiped the app away and reopened it), and zeroing the
    // struct would leak all of it.
    ml_str_list_free(&g_state.library);
    ml_str_list_free(&g_state.music_view);
    ml_str_list_free(&g_state.rows);
    memset(&g_state, 0, sizeof(g_state));

    g_state.app = app;
    g_state.screen = g_process_ever_initialized ? g_last_screen : SCREEN_MUSICA;
    // Never restore INTO the modal DAC question: whether it should be up
    // is owned by native_bridge, and the main loop raises it if so.
    if (g_state.screen == SCREEN_DAC_QUESTION) g_state.screen = SCREEN_MUSICA;
    g_state.theme_black = 1;
    g_state.musica_selected = -1;
    g_state.font_scale = 2;
    refresh_density(app);

    if (!g_process_ever_initialized) {
        pc_init();
        g_process_ever_initialized = true;
    }

    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;

    init_music_library_paths(app);
    load_music_library();

    while (1) {
        int events;
        struct android_poll_source *source;
        int timeout_ms = pc_ms_until_next_tick();

        // No window means nothing to redraw, so the once-a-second clock
        // tick has nothing to tick -- without this check a backgrounded
        // app kept waking up to call a draw_frame() that returns
        // immediately.
        bool player_live = g_state.window != NULL && (g_state.screen == SCREEN_PLAYER) &&
                           g_state.snap.is_playing && !g_state.snap.is_paused;
        if (player_live) {
            // Wake up exactly when the mm:ss readout is about to change
            // instead of twice a second regardless. Same visible clock,
            // roughly half the redraws -- and a redraw is a full-screen
            // clear plus a few hundred glyph blits.
            double frac = g_state.snap.position_seconds - (double)(long long)g_state.snap.position_seconds;
            int until_tick = (int)((1.0 - frac) * 1000.0) + 10;
            if (until_tick < 50) until_tick = 50;
            if (timeout_ms < 0 || timeout_ms > until_tick) timeout_ms = until_tick;
        }
        if (atomic_load(&g_scan_running)) {
            // Keep the "(scanning...)" state responsive without polling
            // hard: this only runs while a scan is genuinely in flight.
            if (timeout_ms < 0 || timeout_ms > 250) timeout_ms = 250;
        }

        while (ALooper_pollOnce(timeout_ms, NULL, &events, (void **)&source) >= 0) {
            if (source != NULL) source->process(app, source);
            if (app->destroyRequested != 0) {
                // Window/thread teardown only -- playback intentionally
                // keeps running in the background, see APP_CMD_DESTROY
                // above.
                native_bridge_detach_env(app);
                return;
            }
            timeout_ms = 0;
        }

        pc_tick();
        if (g_state.screen != SCREEN_DAC_QUESTION) g_last_screen = g_state.screen;

        // A DAC can be plugged in at any moment, including while the app
        // is backgrounded -- in which case the question simply appears
        // the next time there is a window to draw it on.
        bool question = native_bridge_dac_question_pending();
        if (question && g_state.screen != SCREEN_DAC_QUESTION) {
            g_state.dac_return_screen = g_state.screen;
            g_state.screen = SCREEN_DAC_QUESTION;
            g_state.ui_dirty = 1;
        } else if (!question && g_state.screen == SCREEN_DAC_QUESTION) {
            // Answered, or the DAC was unplugged before it was answered.
            g_state.screen = g_state.dac_return_screen;
            g_state.ui_dirty = 1;
        }

        if (native_bridge_consume_rescan_request()) {
            start_scan_if_idle();
            g_state.ui_dirty = 1;
        }
        if (consume_scan_result()) {
            g_state.ui_dirty = 1;
        }
        if (player_live) g_state.ui_dirty = 1;
        if (pc_consume_dirty() || g_state.ui_dirty) {
            g_state.ui_dirty = 0;
            draw_frame();
        }
    }
}
