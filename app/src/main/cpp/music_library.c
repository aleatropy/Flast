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

// Deliberately no _POSIX_C_SOURCE here. Everything this file uses
// (opendir/readdir, lstat, mkdir, rename, strcasecmp) is exposed by
// default on Bionic, and pinning the feature-test macro to POSIX-only
// hides the DT_REG/DT_DIR/DT_LNK constants from <dirent.h> on glibc --
// which breaks compiling this file on a host for testing, for no gain
// on the target.
#include "music_library.h"

#include <android/log.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "FlastScan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define ML_MAX_PATH 1024
#define ML_MAX_DEPTH 24

// A pathological filesystem (a symlink farm, a bind-mount loop the
// symlink check somehow misses, a device with a genuinely enormous
// sample library) must not be able to grow this process without bound.
// Hitting the cap is logged loudly rather than silently truncating.
#define ML_MAX_TRACKS 100000

static char g_files_dir[ML_MAX_PATH] = {0};

#define ML_MAX_ROOTS 8
static char g_extra_roots[ML_MAX_ROOTS][ML_MAX_PATH];
static int g_extra_root_count = 0;

void ml_set_storage_roots(const char *const *roots, int count) {
    g_extra_root_count = 0;
    for (int i = 0; i < count && g_extra_root_count < ML_MAX_ROOTS; i++) {
        if (roots[i] == NULL || roots[i][0] != '/') continue;
        snprintf(g_extra_roots[g_extra_root_count], ML_MAX_PATH, "%s", roots[i]);
        g_extra_root_count++;
    }
    LOGI("ml_set_storage_roots: %d root(s) from the framework", g_extra_root_count);
}

void ml_init(const char *internal_data_path) {
    if (internal_data_path == NULL || internal_data_path[0] == '\0') {
        LOGE("ml_init: no internal data path -- cache and playlists are unavailable");
        g_files_dir[0] = '\0';
        return;
    }
    snprintf(g_files_dir, sizeof(g_files_dir), "%s", internal_data_path);
    LOGI("ml_init: files dir = %s", g_files_dir);
}

bool ml_ready(void) { return g_files_dir[0] != '\0'; }

void ml_str_list_free(StrList *list) {
    if (list == NULL) return;
    if (list->owns_arena) free(list->arena);
    free(list->items);
    list->arena = NULL;
    list->items = NULL;
    list->count = 0;
    list->owns_arena = false;
}

// ---------------------------------------------------------------------
// Growable text arena
// ---------------------------------------------------------------------
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    int count;
} Arena;

static bool arena_reserve(Arena *a, size_t extra) {
    if (a->len + extra <= a->cap) return true;
    size_t want = a->cap ? a->cap * 2 : 16384;
    while (want < a->len + extra) want *= 2;
    char *grown = (char *)realloc(a->buf, want);
    if (grown == NULL) return false;
    a->buf = grown;
    a->cap = want;
    return true;
}

static bool arena_push(Arena *a, const char *s, size_t n) {
    if (!arena_reserve(a, n + 1)) return false;
    memcpy(a->buf + a->len, s, n);
    a->len += n;
    a->buf[a->len++] = '\0';
    a->count++;
    return true;
}

static int cmp_ci(const void *a, const void *b) {
    return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

// Turns a finished arena into a StrList, sorted case-insensitively and
// de-duplicated. `arena` is consumed either way (the StrList takes it,
// or it is freed on failure).
static bool arena_finish(Arena *a, StrList *out, bool sort_and_dedupe) {
    out->arena = a->buf;
    out->items = NULL;
    out->count = 0;
    out->owns_arena = true;
    a->buf = NULL;

    if (a->count <= 0) {
        return true;
    }

    char **items = (char **)malloc(sizeof(char *) * (size_t)a->count);
    if (items == NULL) {
        free(out->arena);
        out->arena = NULL;
        return false;
    }

    char *p = out->arena;
    for (int i = 0; i < a->count; i++) {
        items[i] = p;
        p += strlen(p) + 1;
    }

    if (sort_and_dedupe) {
        qsort(items, (size_t)a->count, sizeof(char *), cmp_ci);
        int w = 0;
        for (int i = 0; i < a->count; i++) {
            if (w > 0 && strcmp(items[w - 1], items[i]) == 0) continue;
            items[w++] = items[i];
        }
        out->count = w;
    } else {
        out->count = a->count;
    }

    out->items = items;
    return true;
}

// ---------------------------------------------------------------------
// Filesystem scan
// ---------------------------------------------------------------------
static bool has_flac_extension(const char *name) {
    size_t n = strlen(name);
    return n > 5 && strcasecmp(name + n - 5, ".flac") == 0;
}

typedef struct {
    Arena *arena;
    char path[ML_MAX_PATH];
    size_t path_len;
    int skipped_dirs;
    bool capped;
} ScanCtx;

// Exactly the same skip rules the Kotlin scan used, no more: hidden
// directories, plus Android/data and Android/obb, which are other apps'
// scoped-storage sandboxes -- unreadable to us, and on a device with
// many apps also the deepest and widest part of the tree, so skipping
// them is most of the scan-time saving.
//
// Nothing else is filtered on purpose. It is tempting to also skip
// things like LOST.DIR or a "thumbnails" folder, but this app's whole
// scanning premise (see the note on collect_roots) is that people put
// music in folders nobody can predict, and a skip rule that guesses
// wrong makes a user's tracks silently invisible with no way to tell.
//
// `parent_len` is the length of ctx->path BEFORE `name` was appended.
// Taking the parent's name from ctx->path directly was the bug this
// signature exists to prevent: by the time this is called ctx->path
// already ends in `name`, so "is my parent called Android?" was really
// asking "am I called Android?" and Android/data was being walked.
static bool is_skipped_dir(const ScanCtx *ctx, const char *name, size_t parent_len) {
    if (name[0] == '.') return true; // hidden, plus "." and ".."
    if (strcmp(name, "data") == 0 || strcmp(name, "obb") == 0) {
        const char *parent = ctx->path;
        for (size_t i = 0; i < parent_len; i++) {
            if (ctx->path[i] == '/') parent = ctx->path + i + 1;
        }
        size_t parent_name_len = parent_len - (size_t)(parent - ctx->path);
        if (parent_name_len == 7 && strncmp(parent, "Android", 7) == 0) return true;
    }
    return false;
}

static void scan_dir(ScanCtx *ctx, int depth) {
    if (depth > ML_MAX_DEPTH || ctx->capped) return;

    DIR *dir = opendir(ctx->path);
    if (dir == NULL) {
        // One unreadable directory must never abort the rest of the
        // volume. (The Kotlin walkTopDown() this replaces got that
        // right too, and it is worth keeping: OEM-restricted folders
        // are common and hitting one used to lose everything after it.)
        ctx->skipped_dirs++;
        return;
    }

    size_t base_len = ctx->path_len;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        size_t name_len = strlen(name);
        if (base_len + 1 + name_len + 1 >= sizeof(ctx->path)) continue;

        unsigned char type = ent->d_type;
        if (type == DT_LNK) {
            // Never follow a symlink during traversal. This is the
            // whole defence against a symlink loop turning the scan
            // into an infinite walk -- Java's File.isDirectory() (what
            // the previous Kotlin scan used) follows them, so a single
            // self-referential link anywhere under /storage could spin
            // the scan until the depth cap, 24 levels deep, on every
            // branch.
            continue;
        }

        // Build "<ctx->path>/<name>" in place.
        ctx->path[base_len] = '/';
        memcpy(ctx->path + base_len + 1, name, name_len + 1);
        ctx->path_len = base_len + 1 + name_len;

        if (type == DT_UNKNOWN) {
            // Some filesystems (and Android's FUSE layer, occasionally)
            // do not fill in d_type. Only then is a syscall per entry
            // worth paying; DT_REG/DT_DIR cover the normal case for
            // free.
            struct stat st;
            if (lstat(ctx->path, &st) != 0) {
                ctx->path[base_len] = '\0';
                ctx->path_len = base_len;
                continue;
            }
            if (S_ISLNK(st.st_mode)) type = DT_LNK;
            else if (S_ISDIR(st.st_mode)) type = DT_DIR;
            else if (S_ISREG(st.st_mode)) type = DT_REG;
            else type = DT_UNKNOWN;
        }

        if (type == DT_DIR) {
            if (!is_skipped_dir(ctx, name, base_len)) {
                scan_dir(ctx, depth + 1);
            }
        } else if (type == DT_REG && has_flac_extension(name)) {
            if (ctx->arena->count >= ML_MAX_TRACKS) {
                if (!ctx->capped) {
                    LOGE("scan: stopping at %d tracks -- library cap reached, results are "
                         "INCOMPLETE", ML_MAX_TRACKS);
                    ctx->capped = true;
                }
            } else if (!arena_push(ctx->arena, ctx->path, ctx->path_len)) {
                LOGE("scan: out of memory at %d tracks", ctx->arena->count);
                ctx->capped = true;
            }
        }

        ctx->path[base_len] = '\0';
        ctx->path_len = base_len;

        if (ctx->capped) break;
    }

    closedir(dir);
}

// Every place a mounted volume can show up, deduplicated by
// (device, inode) so the /sdcard -> /storage/self/primary ->
// /storage/emulated/0 chain of symlinks is walked exactly once instead
// of two or three times.
typedef struct {
    char paths[ML_MAX_ROOTS][ML_MAX_PATH];
    dev_t dev[ML_MAX_ROOTS];
    ino_t ino[ML_MAX_ROOTS];
    int count;
} Roots;

static void roots_add(Roots *r, const char *path) {
    if (r->count >= ML_MAX_ROOTS || path == NULL || path[0] == '\0') return;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) return;
    if (access(path, R_OK | X_OK) != 0) return;

    for (int i = 0; i < r->count; i++) {
        if (r->dev[i] == st.st_dev && r->ino[i] == st.st_ino) return;
    }

    snprintf(r->paths[r->count], ML_MAX_PATH, "%s", path);
    r->dev[r->count] = st.st_dev;
    r->ino[r->count] = st.st_ino;
    r->count++;
}

static void collect_roots(Roots *r) {
    r->count = 0;

    // Framework-reported volumes first: this is the only source that
    // reliably includes a removable SD card (see ml_set_storage_roots).
    for (int i = 0; i < g_extra_root_count; i++) {
        roots_add(r, g_extra_roots[i]);
    }

    // Primary shared storage, by the three names Android has used for
    // it. EXTERNAL_STORAGE is what the platform itself exports.
    roots_add(r, getenv("EXTERNAL_STORAGE"));
    roots_add(r, "/storage/emulated/0");
    roots_add(r, "/sdcard");

    // Backstop only. On a real device /storage is drwx--x--x, so this
    // opendir FAILS for an app and finds nothing -- which is exactly why
    // ml_set_storage_roots() above exists. Kept because it costs one
    // failed syscall and does work on some ROMs and on emulators, where
    // it is the cheapest way to notice a second volume.
    DIR *dir = opendir("/storage");
    if (dir != NULL) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && r->count < ML_MAX_ROOTS) {
            const char *name = ent->d_name;
            if (name[0] == '.') continue;
            if (strcmp(name, "self") == 0 || strcmp(name, "emulated") == 0) continue;
            if (strcmp(name, "container") == 0 || strcmp(name, "knox-emulated") == 0) continue;
            char full[ML_MAX_PATH];
            snprintf(full, sizeof(full), "/storage/%s", name);
            roots_add(r, full);
        }
        closedir(dir);
    }
}

static bool cache_path(char *out, size_t n) {
    return snprintf(out, n, "%s/music_scan_cache.txt", g_files_dir) < (int)n;
}

// ---------------------------------------------------------------------
// The scan cache is by far the largest thing this app ever writes to the
// user's device -- it was measured at ~390 KB of "app data" on a real
// library, several times the size of the APK itself.
//
// Nearly all of that is repetition. The list is sorted by full path, so
// consecutive entries share long prefixes:
//
//   /storage/emulated/0/Music/Pink Floyd/Animals/01 Pigs on the Wing.flac
//   /storage/emulated/0/Music/Pink Floyd/Animals/02 Dogs.flac
//                                                ^ 48 identical bytes
//
// Front-coding stores each line as "<bytes shared with the previous
// line>\t<the rest>", which on a real folder-per-album library removes
// roughly 60-75% of the file. Nothing about the in-memory library
// changes: paths are expanded back to full strings on load.
//
// A version marker on the first line makes this safe both ways. A v1.0
// cache (plain paths, no marker) is simply not accepted, and an entry
// whose shared-prefix count is impossible makes the whole file suspect.
// In both cases the cache is discarded and a rescan runs -- the failure
// mode is "one extra scan", never "a wrong library".
// ---------------------------------------------------------------------
#define ML_CACHE_MAGIC "FLASTCACHE2"

static void save_cache(const StrList *list) {
    if (!ml_ready()) return;
    char path[ML_MAX_PATH];
    char tmp[ML_MAX_PATH + 8];
    if (!cache_path(path, sizeof(path))) return;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "wb");
    if (f == NULL) {
        LOGW("save_cache: cannot write %s (%s)", tmp, strerror(errno));
        return;
    }
    fputs(ML_CACHE_MAGIC "\n", f);

    const char *prev = "";
    size_t prev_len = 0;
    for (int i = 0; i < list->count; i++) {
        const char *cur = list->items[i];
        size_t cur_len = strlen(cur);
        size_t shared = 0;
        size_t max_shared = prev_len < cur_len ? prev_len : cur_len;
        while (shared < max_shared && prev[shared] == cur[shared]) shared++;
        fprintf(f, "%zu\t%s\n", shared, cur + shared);
        prev = cur;
        prev_len = cur_len;
    }

    // Write to a temp file and rename: a scan interrupted by the
    // process being killed then leaves the previous good cache intact
    // instead of a half-written one that looks like a tiny library.
    bool ok = (fflush(f) == 0) && (ferror(f) == 0);
    long written = ftell(f);
    fclose(f);
    if (ok && rename(tmp, path) == 0) {
        LOGI("cache saved: %d paths, %ld bytes on disk", list->count, written);
    } else {
        LOGW("save_cache: could not finalise %s", path);
        unlink(tmp);
    }
}

bool ml_scan(StrList *out) {
    memset(out, 0, sizeof(*out));
    Arena arena = {0};

    Roots roots;
    collect_roots(&roots);
    LOGI("scan: %d storage root(s)", roots.count);

    ScanCtx ctx;
    ctx.arena = &arena;
    ctx.skipped_dirs = 0;
    ctx.capped = false;

    for (int i = 0; i < roots.count; i++) {
        snprintf(ctx.path, sizeof(ctx.path), "%s", roots.paths[i]);
        ctx.path_len = strlen(ctx.path);
        // Strip a trailing slash so paths do not come out as "//Music".
        while (ctx.path_len > 1 && ctx.path[ctx.path_len - 1] == '/') {
            ctx.path[--ctx.path_len] = '\0';
        }
        int before = arena.count;
        scan_dir(&ctx, 0);
        LOGI("scan: %s -> %d file(s)", roots.paths[i], arena.count - before);
    }

    if (ctx.skipped_dirs > 0) {
        LOGI("scan: skipped %d unreadable director%s, continued with the rest",
             ctx.skipped_dirs, ctx.skipped_dirs == 1 ? "y" : "ies");
    }

    if (!arena_finish(&arena, out, true)) {
        LOGE("scan: out of memory building the index");
        return false;
    }

    LOGI("scan complete: %d .flac file(s)", out->count);
    save_cache(out);
    return true;
}

bool ml_load_cache(StrList *out) {
    memset(out, 0, sizeof(*out));
    if (!ml_ready()) return false;

    char path[ML_MAX_PATH];
    if (!cache_path(path, sizeof(path))) return false;

    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;

    char line[ML_MAX_PATH + 32];
    if (fgets(line, sizeof(line), f) == NULL ||
        strncmp(line, ML_CACHE_MAGIC, sizeof(ML_CACHE_MAGIC) - 1) != 0) {
        // No marker: either a v1.0 plain-path cache or something
        // corrupt. Either way, rescan rather than guess.
        LOGI("cache has no %s marker -- ignoring it and rescanning", ML_CACHE_MAGIC);
        fclose(f);
        return false;
    }

    Arena arena = {0};
    char prev[ML_MAX_PATH];
    size_t prev_len = 0;
    prev[0] = '\0';
    bool corrupt = false;

    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;

        char *tab = strchr(line, '\t');
        if (tab == NULL) { corrupt = true; break; }
        *tab = '\0';
        char *endp = NULL;
        unsigned long shared = strtoul(line, &endp, 10);
        if (endp == line || *endp != '\0' || shared > prev_len) { corrupt = true; break; }

        const char *suffix = tab + 1;
        size_t suffix_len = strlen(suffix);
        if (shared + suffix_len >= sizeof(prev)) { corrupt = true; break; }

        memcpy(prev + shared, suffix, suffix_len + 1);
        prev_len = shared + suffix_len;
        if (prev_len == 0) continue;
        if (!arena_push(&arena, prev, prev_len)) { corrupt = true; break; }
    }
    fclose(f);

    if (corrupt) {
        LOGW("cache is malformed -- discarding it and rescanning");
        free(arena.buf);
        return false;
    }

    // Already sorted and deduplicated when it was written.
    if (!arena_finish(&arena, out, false) || out->count == 0) {
        ml_str_list_free(out);
        return false;
    }
    LOGI("cache loaded: %d paths", out->count);
    return true;
}

// ---------------------------------------------------------------------
// DAC volume answers
// ---------------------------------------------------------------------
#define ML_DAC_PREFS_NAME "dac_volume_prefs.txt"

static bool dac_prefs_path(char *out, size_t n) {
    if (!ml_ready()) return false;
    return snprintf(out, n, "%s/%s", g_files_dir, ML_DAC_PREFS_NAME) < (int)n;
}

int ml_get_dac_answer(int vendor_id, int product_id) {
    char path[ML_MAX_PATH];
    if (!dac_prefs_path(path, sizeof(path))) return -1;
    FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;

    int answer = -1;
    char line[128];
    while (fgets(line, sizeof(line), f) != NULL) {
        int v = 0, p = 0;
        char verdict[8] = {0};
        // Same on-disk format the Kotlin implementation wrote, so answers
        // saved by an earlier install are still honoured.
        if (sscanf(line, "%d:%d:%7s", &v, &p, verdict) == 3 &&
            v == vendor_id && p == product_id) {
            answer = (strncmp(verdict, "SI", 2) == 0) ? 1 : 0;
            /* keep scanning: a later line for the same device wins */
        }
    }
    fclose(f);
    LOGI("ml_get_dac_answer(%d, %d) = %d", vendor_id, product_id, answer);
    return answer;
}

bool ml_set_dac_answer(int vendor_id, int product_id, bool has_own_control) {
    char path[ML_MAX_PATH];
    if (!dac_prefs_path(path, sizeof(path))) return false;

    // Read everything except this device, then rewrite with the new answer
    // appended. The file holds one short line per DAC a person has ever
    // plugged in, so rewriting it whole costs nothing.
    Arena keep = {0};
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        char line[128];
        while (fgets(line, sizeof(line), f) != NULL) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
            if (len == 0) continue;
            int v = 0, p = 0;
            char verdict[8] = {0};
            if (sscanf(line, "%d:%d:%7s", &v, &p, verdict) == 3 &&
                v == vendor_id && p == product_id) {
                continue; /* superseded */
            }
            arena_push(&keep, line, len);
        }
        fclose(f);
    }

    char tmp[ML_MAX_PATH + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *out = fopen(tmp, "wb");
    if (out == NULL) {
        free(keep.buf);
        LOGW("ml_set_dac_answer: cannot write %s (%s)", tmp, strerror(errno));
        return false;
    }
    const char *p2 = keep.buf;
    for (int i = 0; i < keep.count && p2 != NULL; i++) {
        fputs(p2, out);
        fputc('\n', out);
        p2 += strlen(p2) + 1;
    }
    fprintf(out, "%d:%d:%s\n", vendor_id, product_id, has_own_control ? "SI" : "NO");
    bool ok = (fflush(out) == 0) && (ferror(out) == 0);
    fclose(out);
    free(keep.buf);

    if (ok && rename(tmp, path) == 0) {
        LOGI("ml_set_dac_answer(%d, %d) = %d", vendor_id, product_id, (int)has_own_control);
        return true;
    }
    unlink(tmp);
    return false;
}

// ---------------------------------------------------------------------
// Playlists
// ---------------------------------------------------------------------
static bool playlist_name_ok(const char *name) {
    if (name == NULL || name[0] == '\0') return false;
    if (strchr(name, '/') != NULL) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    return strlen(name) < 96;
}

static bool playlists_dir(char *out, size_t n) {
    if (!ml_ready()) return false;
    if (snprintf(out, n, "%s/playlists", g_files_dir) >= (int)n) return false;
    mkdir(out, 0700); // EEXIST is the normal case and is fine
    return true;
}

static bool playlist_path(const char *name, char *out, size_t n) {
    if (!playlist_name_ok(name)) return false;
    char dir[ML_MAX_PATH];
    if (!playlists_dir(dir, sizeof(dir))) return false;
    return snprintf(out, n, "%s/%s.txt", dir, name) < (int)n;
}

bool ml_list_playlists(StrList *out) {
    memset(out, 0, sizeof(*out));
    char dir[ML_MAX_PATH];
    if (!playlists_dir(dir, sizeof(dir))) return false;

    DIR *d = opendir(dir);
    if (d == NULL) return false;

    Arena arena = {0};
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len <= 4 || strcmp(name + len - 4, ".txt") != 0) continue;
        if (name[0] == '.') continue;
        arena_push(&arena, name, len - 4); // without the .txt
    }
    closedir(d);

    return arena_finish(&arena, out, true);
}

// Reads a text file (one entry per line) into a StrList. Shares the
// whole "file becomes the arena" trick with ml_load_cache().
static bool load_lines(const char *path, StrList *out) {
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;

    Arena arena = {0};
    char line[ML_MAX_PATH];
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;
        if (!arena_push(&arena, line, len)) break;
    }
    fclose(f);

    return arena_finish(&arena, out, false);
}

bool ml_load_playlist(const char *name, StrList *out) {
    memset(out, 0, sizeof(*out));
    char path[ML_MAX_PATH + 128];
    if (!playlist_path(name, path, sizeof(path))) return false;
    return load_lines(path, out);
}

bool ml_add_to_playlist(const char *name, const char *path) {
    char file[ML_MAX_PATH + 128];
    if (!playlist_path(name, file, sizeof(file))) return false;
    if (path == NULL || path[0] == '\0') return false;

    // Skip duplicates, matching the previous behaviour.
    StrList existing;
    if (load_lines(file, &existing)) {
        for (int i = 0; i < existing.count; i++) {
            if (strcmp(existing.items[i], path) == 0) {
                ml_str_list_free(&existing);
                return true;
            }
        }
        ml_str_list_free(&existing);
    }

    FILE *f = fopen(file, "ab");
    if (f == NULL) {
        LOGW("ml_add_to_playlist: cannot open %s (%s)", file, strerror(errno));
        return false;
    }
    fputs(path, f);
    fputc('\n', f);
    fclose(f);
    return true;
}

bool ml_create_auto_playlist(char *out_name, size_t out_sz) {
    StrList existing;
    bool have = ml_list_playlists(&existing);

    int n = 1;
    char candidate[64];
    while (n < 10000) {
        snprintf(candidate, sizeof(candidate), "Playlist %d", n);
        bool taken = false;
        if (have) {
            for (int i = 0; i < existing.count; i++) {
                if (strcmp(existing.items[i], candidate) == 0) { taken = true; break; }
            }
        }
        if (!taken) break;
        n++;
    }
    if (have) ml_str_list_free(&existing);

    char path[ML_MAX_PATH + 128];
    if (!playlist_path(candidate, path, sizeof(path))) return false;
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        LOGW("ml_create_auto_playlist: cannot create %s (%s)", path, strerror(errno));
        return false;
    }
    fclose(f);

    snprintf(out_name, out_sz, "%s", candidate);
    LOGI("playlist created: %s", candidate);
    return true;
}

void ml_delete_playlist(const char *name) {
    char path[ML_MAX_PATH + 128];
    if (!playlist_path(name, path, sizeof(path))) return;
    if (unlink(path) == 0) {
        LOGI("playlist deleted: %s", name);
    }
}
