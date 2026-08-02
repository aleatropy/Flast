#!/usr/bin/env bash
# Host test harnesses for the native layer.
#
# These are NOT part of the Android build and add nothing to the APK.
# They compile the *actual shipped source* of the scanner, the renderer
# and the audio ring buffer for the host and run it under
# AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer --
# the parts of this codebase where a bug is silent (a path silently not
# found, a pixel silently written out of bounds, a sample silently
# corrupted by a race) rather than loud.
#
# Two of the suites do not test a copy of the logic: test_album_views.c
# and test_ring_buffer.c #include a generated .inc that is extracted
# verbatim from ui_main.c and flac_stream.c at run time, so they cannot
# drift away from what actually ships.
#
# Usage:  tools/tests/run.sh
# Needs:  gcc (or clang) and python3. No Android SDK/NDK required.

set -u
cd "$(dirname "$0")/../.."
ROOT="$PWD"
CPP="$ROOT/app/src/main/cpp"
OUT="${TMPDIR:-/tmp}/flast-tests"
STUB="$ROOT/tools/tests/stub"
CC="${CC:-gcc}"
mkdir -p "$OUT"

ASAN="-fsanitize=address,undefined"
COMMON="-std=gnu11 -g -Wall -Wextra -I$CPP -I$STUB -I$OUT"

# Extract the functions under test verbatim from the shipped sources, so
# these suites test the real code rather than a copy that can rot.
python3 - "$CPP" "$OUT" <<'PY'
import sys
cpp, out = sys.argv[1], sys.argv[2]

def grab(src, sig):
    i = src.index(sig)
    j = src.index('{', i)
    d, k = 0, j
    while True:
        if src[k] == '{': d += 1
        elif src[k] == '}':
            d -= 1
            if d == 0: break
        k += 1
    return src[i:k + 1] + "\n"

ui = open(f"{cpp}/ui_main.c").read()
open(f"{out}/view_extract.inc", "w").write("\n".join(grab(ui, s) for s in [
    'static const char *filename_of(',
    'static const StrList *music_list(',
    'static int music_count(',
    'static const char *music_path_at(',
    'static const char *music_label_at(',
    'static void set_music_mode_all(void) {',
    'static bool is_disc_folder(',
    'static void album_of(',
    'static int cmp_ci_ptr(',
    'static void build_album_list(',
    'static void filter_music_by_album(',
]))

fs = open(f"{cpp}/flac_stream.c").read()
parts = [fs[fs.index('typedef struct {\n    int32_t *data;'):fs.index('} RingBuffer;') + 13] + "\n"]
parts += [grab(fs, s) for s in [
    'static bool ring_buffer_init(',
    'static void ring_buffer_destroy(',
    'static size_t ring_buffer_write(',
    'static void convert_samples(',
    'static size_t ring_buffer_read(',
]]
open(f"{out}/ring_extract.inc", "w").write("\n".join(parts))
print("  extracted view_extract.inc and ring_extract.inc from the shipped sources")
PY

# Never run a stale binary from a previous successful build: a compile
# error would otherwise show up as the OLD test still passing.
rm -f "$OUT"/t_ml "$OUT"/t_cache "$OUT"/t_view "$OUT"/t_render "$OUT"/t_ring_a "$OUT"/t_ring_t

fail=0
run() { # run <label> <binary> <env...>
  local label="$1"; shift
  local bin="$1"; shift
  printf '  %-16s ' "$label"
  if [ ! -x "$bin" ]; then
    fail=1
    echo "NOT BUILT (compile failed above)"
    return
  fi
  if out=$(env "$@" "$bin" 2>&1); then
    echo "$out" | tail -1
  else
    fail=1
    echo "FAILED"
    echo "$out" | sed 's/^/      /'
  fi
}

echo "building..."
$CC $COMMON $ASAN -o "$OUT/t_ml"     tools/tests/test_music_library.c "$CPP/music_library.c" || fail=1
$CC $COMMON $ASAN -o "$OUT/t_cache"  tools/tests/test_cache_format.c  "$CPP/music_library.c" || fail=1
$CC $COMMON $ASAN -o "$OUT/t_view"   tools/tests/test_album_views.c   "$CPP/music_library.c" || fail=1
$CC $COMMON $ASAN -o "$OUT/t_render" tools/tests/test_render.c "$CPP/ui_render.c" "$CPP/ui_font_spleen8x16.c" || fail=1
$CC $COMMON $ASAN -o "$OUT/t_ring_a" tools/tests/test_ring_buffer.c -lpthread || fail=1
$CC $COMMON -fsanitize=thread -o "$OUT/t_ring_t" tools/tests/test_ring_buffer.c -lpthread || fail=1

echo "running..."
rm -rf "$OUT/tree" "$OUT/files"; mkdir -p "$OUT/files"
run "music_library"  "$OUT/t_ml"     "TESTDIR=$OUT/tree" "EXTERNAL_STORAGE=$OUT/tree/storage"
run "cache_format"   "$OUT/t_cache"  "FILESDIR=$OUT/files" "EXTERNAL_STORAGE=$OUT/tree/storage"
run "album_views"    "$OUT/t_view"
run "render"         "$OUT/t_render"
run "ring (ASan)"    "$OUT/t_ring_a"
run "ring (TSan)"    "$OUT/t_ring_t"

[ $fail -eq 0 ] && echo "OK" || echo "SOME SUITES FAILED"
exit $fail
