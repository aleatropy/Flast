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

#ifndef FLAST_MUSIC_LIBRARY_H
#define FLAST_MUSIC_LIBRARY_H

#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------
// Library storage: one arena + one index, never N little allocations.
//
// The previous design held every path as its own strdup()'d block, and
// held a second strdup()'d copy of each filename next to it as a
// "label", and then made a THIRD and FOURTH copy of both for whichever
// view was on screen. For a 3,000-track library that is ~12,000 heap
// blocks; on a 64-bit allocator each one costs ~16 bytes of header and
// rounding on top of its contents, so the bookkeeping alone was ~190KB
// -- more than the paths themselves.
//
// Here, all the path text lives in one contiguous `arena` of
// NUL-terminated strings and `items` is an index into it. Two
// allocations for the whole library, no per-string overhead, and
// freeing it is two free() calls instead of walking 12,000 pointers.
//
// Labels are gone entirely: the label of a track IS the part of its
// path after the last '/', so it is computed by pointer arithmetic at
// draw time instead of being stored twice.
// ---------------------------------------------------------------------
typedef struct {
    char *arena;    // all strings back to back, each NUL-terminated
    char **items;   // count pointers into arena (or, for borrowed
                    // views, into another StrList's arena)
    int count;
    bool owns_arena; // false => this is a view onto someone else's text
} StrList;

void ml_str_list_free(StrList *list);

// Must be called once before anything else. `internal_data_path` is
// ANativeActivity's internalDataPath (the app's private files dir).
void ml_init(const char *internal_data_path);
bool ml_ready(void);

// Storage volume roots discovered by the framework (StorageManager /
// getExternalFilesDirs) and handed down from the Kotlin layer.
//
// This is NOT redundant with the /storage sweep in collect_roots(). On a
// real device /storage is drwx--x--x: an app may traverse INTO a known
// path under it but cannot list it, so opendir("/storage") fails and a
// removable SD card mounted at /storage/XXXX-XXXX is invisible to a
// pure-C scan. Measured on a Galaxy A55: 658 .flac files on the SD card,
// found by exactly zero of them until this existed.
//
// Safe to call with count == 0; the built-in candidates still apply.
void ml_set_storage_roots(const char *const *roots, int count);

// Reads <files>/music_scan_cache.txt.
bool ml_load_cache(StrList *out);

// Full recursive scan of every mounted storage volume, sorted
// case-insensitively by full path, written back to the cache file.
// Safe to run on a worker thread. Returns false only if nothing could
// be read at all.
bool ml_scan(StrList *out);

// Playlists are plain text files in <files>/playlists/<name>.txt, one
// absolute track path per line -- the same on-disk format the Kotlin
// implementation used, so existing playlists survive this change.
bool ml_list_playlists(StrList *out);
bool ml_load_playlist(const char *name, StrList *out);
bool ml_add_to_playlist(const char *name, const char *path);
bool ml_create_auto_playlist(char *out_name, size_t out_sz);
void ml_delete_playlist(const char *name);

#endif
