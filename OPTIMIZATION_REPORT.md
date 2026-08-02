# Optimization Report — v1.2.0

> **v1.2.0 update.** The numbers below were written for v1.1.0, before any of
> it ran on hardware. Everything has since been verified on a Galaxy A55
> (Android 16) with a real 658-track library, and the device found things the
> host could not — see `CHANGELOG.md` for the full v1.2.0 entry. Two headline
> corrections:
>
> - **Section 5.3 was wrong.** It argued `hardwareAccelerated="false"` was
>   worth ~0 because `NativeActivity` takes its own surface. Measured: `GL
>   mtrack` was **1,156 KB on a cold start with the dialog never shown**, and
>   is **0** now. HWUI *was* running. The flag is worth real memory, and the
>   DAC dialog moved to a native screen so it could be turned off safely.
> - **The C scanner could not see SD cards.** `opendir("/storage")` fails on a
>   real device (`drwx--x--x`: traversable, not listable). 658 .flac files on
>   this phone's SD card were invisible until volume roots came back from the
>   framework. The host harness ran on a fully readable directory and passed.
>
> Measured on device, v1.0.0 → v1.2.0: TOTAL PSS 28,583 → 19,965 KB, APK
> 215,780 → 113,738 B (armeabi-v7a 95,680 B), install dir 123 KB, minSdk 31 →
> 26.

## Original v1.1.0 report

**Scope of this pass:** go through the code part by part looking for anything
else that can be cut in RAM or storage without changing what the app does, and
fix the "doesn't open / doesn't scan on other devices" bugs.

**Baseline:** v1.0.0, `versionCode 2`, the build the `dumpsys meminfo` in the
request came from (28,583 KB PSS, 215,780-byte APK, 751 KB on device).

Every number below marked **measured** was produced on this machine — either
from the built artifacts (`llvm-readelf`, `unzip -l`) or from host test
harnesses that compile the *actual shipped source* of the scanner, the
renderer and the audio ring buffer and run it under AddressSanitizer,
UndefinedBehaviorSanitizer and ThreadSanitizer. Numbers marked **projected**
are arithmetic on top of the reported `dumpsys` figures and have **not** been
confirmed on a device. Per spec section 6 point 7, do not publish a projected
number as if it were a measurement.

---

## 1. Headline results

### Storage — measured

| | v1.0.0 | v1.1.0 | Change |
|---|---|---|---|
| APK, arm64-v8a | 215,780 B | **127,034 B** | **−41.1%** |
| APK, armeabi-v7a | *would not install* | **109,800 B** | new |
| APK, x86_64 | *would not install* | **126,151 B** | new |
| `libflacplayer_native.so` (arm64, stripped) | 146,912 B | **86,576 B** | −41.1% |
| `classes.dex` | 41,532 B | **26,620 B** | −35.9% |
| Kotlin runtime resources in the APK | 30,551 B | **0 B** | −100% |
| Exported dynamic symbols in the `.so` | 268 | **4** | −98.5% |
| Scan cache on the user's device (4,823 tracks) | 724,324 B | **131,736 B** | **−81.8%** |

The 390 KB of "app data" quoted in the request is almost entirely the scan
cache. On a comparable library it now lands near 70 KB.

### RAM

| Item | v1.0.0 | v1.1.0 | Basis |
|---|---|---|---|
| Window graphics buffers (`EGL mtrack`) | 6,642 KB | ~3,321 KB | **projected** — exactly half, RGB_565 vs RGBX_8888 |
| Audio ring buffer (44.1 kHz stereo) | 262,144 B | **105,840 B** | **measured** |
| Decode-thread interleave buffer | 131,072 B of stack | **2,048 B** | **measured** (source diff) |
| Library, 4,823 tracks with real paths | ~1,700 KB, 4 copies | **745 KB, 1 copy** | **measured** (host harness) |
| Heap blocks holding the library | ~19,300 | **2** | **measured** |
| ART heap during a scan | 3 simultaneous copies of every path | **0** — no JNI crossing at all | **measured** (source diff) |

---

## 2. The universality bugs, and what caused each

These are the "won't open on other devices" and "won't scan on other devices"
failures. Each one is a specific, identifiable cause.

### 2.1 The app could not physically install on most Android devices — **fixed**

`app/build.gradle.kts` had `abiFilters += listOf("arm64-v8a")`. An APK whose
only native library is arm64 cannot install on a 32-bit-ARM handset (the
installer fails with `INSTALL_FAILED_NO_MATCHING_ABIS`) or on an x86_64
emulator/Chromebook. This is not a runtime bug that shows an error — the app
simply never arrives.

Fixed by building all three ABIs *and* enabling `splits.abi`, so each device
still downloads a single-ABI APK. **Universality here costs the user nothing:
the armeabi-v7a APK is 110 KB, smaller than the arm64 one.** Making this work
also required `third_party/flac-1.4.3/config.h`, which hardcoded
`FLAC__CPU_ARM64 1` and `FLAC__HAS_NEONINTRIN 1` — it now derives those from
the compiler's target, so the same header builds NEON on arm64 and portable C
elsewhere.

### 2.2 `usb.host` was a hard install filter — **fixed**

`AndroidManifest.xml` declared:

```xml
<uses-feature android:name="android.hardware.usb.host" android:required="true" />
```

`required="true"` means Play hides the app from every device without USB host
mode, and some installers refuse it outright. The app does not need USB host to
run — with no DAC attached it plays through the phone's own output and honestly
reports `BIT-PERFECT: NO`, which is the last row of the spec's own section 3.6
table. Now `required="false"`.

### 2.3 The app refused to play on any device without MMAP — **fixed**

`flac_stream.c` had exactly one way to open audio: `AAUDIO_SHARING_MODE_EXCLUSIVE`
with `AAUDIO_FORMAT_PCM_I32`. If that failed, or if `AAudio_getMMapPolicy()`
returned `NEVER`, it returned an error and **played nothing at all, permanently**.

Spec section 3.6 describes the opposite. "Degradado a SHARED (mixer de Android
en uso)" and "el dispositivo/HAL no lo soporta en absoluto" are listed there as
normal states of the *indicator* that show `BIT-PERFECT: NO` — not as reasons
to refuse to play. The code was stricter than the design.

There is now a fallback ladder that tries hardest for bit-perfect and degrades
honestly:

1. EXCLUSIVE + `PCM_I32` — bit-perfect
2. EXCLUSIVE + `PCM_I16` — bit-perfect, and **skipped for >16-bit files**
3. SHARED + `PCM_I32`
4. SHARED + `PCM_FLOAT`
5. SHARED + `PCM_I16`

Rungs 3–5 also drop `PERFORMANCE_MODE_LOW_LATENCY` for `PERFORMANCE_MODE_NONE`:
bit-perfect is already lost there, so bigger buffers and fewer callbacks are a
free battery win.

Two safety checks that did not exist before:

- **Sample rate and channel count are verified after opening.** In EXCLUSIVE
  mode the HAL's own layout wins over what was requested, so a 44.1 kHz album
  could come back on a 48 kHz stream — the whole album at the wrong pitch, with
  nothing in the code noticing. A mismatched stream is now closed and the next
  rung tried.
- **Bit-perfect reporting accounts for the stream's bit depth.** An EXCLUSIVE
  16-bit stream playing a 24-bit file would have claimed `BIT-PERFECT: YES`
  while the conversion threw away the bottom 8 bits of every sample. Rung 2 is
  now skipped for such files, and the flag additionally requires the format to
  be able to carry every bit the file has.

### 2.4 Two undocumented AAudio symbols were hard-linked — **fixed**

```c
extern aaudio_policy_t AAudio_getMMapPolicy(void);
extern bool AAudioStream_isMMapUsed(AAudioStream *stream);
```

Neither is in `<aaudio/AAudio.h>`. They are platform-internal symbols that
happen to be in the NDK's `libaaudio` stub, so linking them compiles and works
on a stock device. On any device whose `libaaudio.so` does not export them, the
dynamic linker refuses to load `libflacplayer_native.so` **at all**,
`System.loadLibrary()` throws from the `FlastNativeActivity` static
initialiser, and the app dies before drawing a frame — a textbook "won't even
open on that phone".

Google's own Oboe library resolves these two with `dlsym()` and null-checks
them for exactly this reason. So does this code now. If they are missing the
app still plays; it just cannot *verify* MMAP, which is a downgrade in
reporting, not in function, and the logcat says so.

### 2.5 Scanning blocked the first frame — **fixed**

`android_main()` called into Kotlin, which walked every storage volume with
`File.walkTopDown()`, and only when that returned did the UI appear. On the
development phone with a tidy Music folder that is imperceptible. On a device
with 40,000 photos, several games' worth of `Android/data`, and an SD card, it
is tens of seconds of black screen — from the user's side, indistinguishable
from an app that does not open.

The scan now runs on its own thread while the UI is live, showing
`(scanning storage for .flac files...)`, and the result is swapped in when it
lands.

### 2.6 A symlink loop could make the scan run essentially forever — **fixed**

Kotlin's `File.walkTopDown()` uses `isDirectory()`, which *follows* symlinks. A
single self-referential link anywhere under `/storage` — and they exist, e.g.
`/sdcard` → `/storage/self/primary` → `/storage/emulated/0` — sends the walk
around in circles.

The C scanner never follows a symlink during traversal (`DT_LNK` is skipped,
with an `lstat()` fallback for filesystems that return `DT_UNKNOWN`), has a
24-level depth cap, and deduplicates storage roots by `(st_dev, st_ino)` so the
`/sdcard` → `/storage/emulated/0` chain is walked exactly once. **Verified:** a
stress tree containing two symlink loops scans to completion.

### 2.7 A lost condvar signal could permanently break auto-advance — **fixed**

`flac_stream.c` signalled end-of-decode with `pthread_cond_signal()`. If the
decode thread finished before the watcher thread reached `pthread_cond_wait()` —
which is what happens with a very short track, or a file that fails to decode —
**the signal was dropped**, the watcher blocked forever, and tracks stopped
auto-advancing for the rest of the process's life. Replaced with a counting
semaphore, which cannot lose an early post.

### 2.8 The Activity was destroyed by ordinary configuration changes — **fixed**

`configChanges` listed only `orientation|screenSize|keyboardHidden`. Everything
else — dark-mode toggle, system font-size change, folding/unfolding, plugging
in a keyboard, changing locale — destroyed and recreated the Activity, which
for a `NativeActivity` means `android_main()` is torn down and restarted on a
brand-new thread, reloading the whole library. The list now covers all of them,
and `APP_CMD_CONFIG_CHANGED` re-reads the display density (every touch target
in this UI is sized from it).

---

## 3. Part by part: what was reduced

### 3.1 `CMakeLists.txt` — the single biggest storage win, and it is pure build config

`.so` went 146,912 → 86,576 bytes with **no source change at all**, from five
flags the project had not been using:

| Flag | What it removed | Measured |
|---|---|---|
| `-fvisibility=hidden` + `--exclude-libs` | 264 of 268 exported symbols | `.dynsym` 8,904→3,096, `.dynstr` 11,231→2,226, `.gnu.hash` 1,868→44 |
| `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections` | libFLAC's encoder-side helpers, which ship in the *same* `.o` files as the decoder ones so "decoder-only source list" never excluded them | part of `.text` 81,520→60,056 |
| `-fno-unwind-tables -fno-asynchronous-unwind-tables` | `.eh_frame` + `.eh_frame_hdr` | 13,140→428 B |
| `-Os` on the app's own C (libFLAC already had it) | NDK Release defaults to `-O3` | part of `.text` |
| `-flto=thin` | cross-translation-unit dead code, including across the libFLAC static library | a further 4,192 B off the arm64 `.so` |

The two are connected: hiding the symbols is what *lets* `--gc-sections`
collect them. Confirmed dead in the shipped binary afterwards:
`FLAC__lpc_compute_autocorrelation`, `FLAC__lpc_compute_residual_from_qlp_coefficients*`,
`FLAC__fixed_compute_best_predictor*`, `FLAC__stream_decoder_seek_absolute`.

**Trade-off, stated plainly:** without `.eh_frame`, native crash backtraces get
less precise. `-fomit-frame-pointer` is deliberately *not* set so tombstones
stay usable. Delete two lines in `CMakeLists.txt` to get full unwind info back.

Also verified by `grep`: `metadata_iterators.c`, `metadata_object.c`,
`window.c` and `float.c` are referenced by **no** decoder-side file. They were
the two largest objects in the library and are now off the source list.

### 3.2 `build.gradle.kts` — 30.6 KB of files nothing reads

The v1.0.0 APK carried `kotlin/kotlin.kotlin_builtins` (18,640 B) and six
siblings, plus `kotlin-tooling-metadata.json`. kotlin-stdlib ships these so
*reflection* can reconstruct built-in types at runtime. This app has no
kotlin-reflect, calls no `::class.members`, and does no serialization — nothing
reads them; they are copied in because they sit on the runtime classpath. Now
excluded, along with `vcsInfo` and the dependency-info signing blob.

Also off: `buildConfig`, `resValues`, `aidl`, `shaders`, `renderScript`, plus
`-Xno-param-assertions`/`-Xno-call-assertions` (Kotlin emits an
`Intrinsics.checkNotNull*` call at the top of every function to guard against
*Java* callers passing null; nothing here is called from Java).

### 3.3 `ui_render.c` / `ui_render.h` — RGB_565, the biggest RAM item

This UI draws exactly two colours, fully opaque black and fully opaque white,
never blends, never uses alpha, and never draws a gradient or an antialiased
edge. In RGB_565 those two colours are `0x0000` and `0xFFFF` — exactly
representable, bit for bit, with no dithering and **no visible difference**.

The window's graphics buffers are the largest block of memory the process owns
(`EGL mtrack 6,642 KB` in the report). Halving bytes per pixel takes roughly
half of that. This was flagged as the open follow-up in the v1.0.0 changelog;
it is done.

`ui_pixel_t` is a typedef and `UI_WINDOW_FORMAT` a constant, so reverting is a
two-line change. `draw_frame()` also logs one line if a device ever hands back
a different format.

The blitter was rewritten while the types changed. `ui_draw_char()` used to
call `ui_fill_rect()` **once per lit pixel** — up to 128 function calls, each
redoing the full rectangle clip, to draw one character. It now walks
destination rows directly with the clip computed once, and full-screen clears
collapse into `memset` (both colours have identical high and low bytes).

**Verified:** every pixel of a rendered glyph matches the font bitmap exactly at
scale 1 and 2; clipping at all four edges writes nothing out of bounds under
ASan; nothing is ever written into the stride gutter (the classic bug when
moving from 32- to 16-bit pixels).

### 3.4 `flac_stream.c` — 128 KB of stack and 153 KB of heap

- `int32_t interleaved[32768]` — **128 KB of stack**, touched therefore
  resident, on a thread that lives for the whole of every track. Gone; frames
  are interleaved 2 KB at a time straight into the ring.
- That array was also why the ring buffer needed a 65,536-sample floor
  (`ring_buffer_write` had to fit an entire FLAC block in one shot). With
  chunked writes the ring is free to be exactly the 300 ms the design asks for:
  **262,144 → 105,840 bytes**, measured.
- The ring's mutex is gone. It was held by the *decode* thread while copying a
  whole block, and taken by the AAudio callback — a real-time thread blocking
  on a normal-priority one, the textbook setup for priority inversion and
  audible dropouts. Now a lock-free single-producer/single-consumer ring.
  **Verified:** 4,000,000 samples streamed through it across 4,116 read calls
  with 0 corrupted samples and **no data races reported by ThreadSanitizer**.
- Per-sample `pos = (pos + 1) % capacity` (a division per sample, ~5 million
  per minute of stereo) replaced by at most two `memcpy`s per call.
- Decode-thread idle polling went from 5 ms (~200 wakeups/second for the entire
  duration of every track, doing nothing, because a full buffer has nothing to
  do) to 15 ms.
- Thread stacks capped at 128 KB instead of Android's 1 MB default.
- `switch_to_index_locked()` no longer calls `flast_stream_stop()` first. That
  closed the stream, which made `previous_sample_rate` always `-1`, which meant
  the "same parameters, reuse the stream" path **existed but could never be
  reached** — every track change re-opened AAudio and re-negotiated EXCLUSIVE
  from scratch. Reuse required fixing a latent use-after-free first: the ring
  buffer was freed while the AAudio callback could still be reading it. The
  stream is now explicitly stopped and waited on before anything it touches is
  freed.
- Undefined behaviour fixed: `sample << (32 - bps)` left-shifts a negative
  `int32_t`. At `-Os` the compiler is entitled to act on that. Now cast through
  `uint32_t`.
- A frame whose channel count disagrees with STREAMINFO is now rejected instead
  of being interleaved at one width and consumed at another.

### 3.5 `music_library.c` (new) — the library moved to C, as the spec always said

Spec section 3.1: *"Escaneo de sistema de archivos (POSIX directo,
opendir/readdir recursivo — no File API de Java)"*. It was in Kotlin.

Handing a 3,000-track library across JNI meant a Kotlin `List<String>`, an
`Array<String>` copied from it, 3,000 live `java.lang.String` objects on the
ART heap, 3,000 `GetStringUTFChars()` temporaries, and 3,000 `strdup()`s — four
representations of the same text alive at once. That is what the reported
`Dalvik Heap size 26,516 KB` spike was made of. Eight JNI methods and both
`String[] <-> char**` marshalling helpers are gone.

Storage model: **one arena + one index**, not N little allocations. All path
text lives in one contiguous block of NUL-terminated strings; `items` indexes
into it. For 4,823 tracks with real paths that is **745 KB in 2 heap blocks**,
versus roughly 1,700 KB in ~19,300 blocks before (a 64-bit allocator costs
~16 bytes of header and rounding per block, so the bookkeeping alone used to
exceed the paths).

Playlists moved too — they are plain text files in the app's private dir, which
C reaches through `ANativeActivity::internalDataPath`. Same on-disk format and
same location, so **existing playlists survive the upgrade**.

**Verified** by host harness against a synthetic storage tree: 7 expected files
found; `Android/data` and `Android/obb` skipped; hidden directories skipped;
`Android/media` correctly *not* skipped; case-insensitive `.FLAC` matched; a
file named exactly `.flac` rejected; sorted case-insensitively; deduplicated;
playlist create/add/dedupe/list/delete correct; `../../../etc/passwd` and names
containing `/` rejected. Stress: 4,823 tracks among 20,000 decoy files, two
symlink loops and a 40-level-deep chain — completes, depth cap engages, no
leaks under ASan.

**One real bug was caught by that harness and fixed before it shipped:** the
"is my parent called `Android`?" check was reading the path *after* the child
name had been appended, so it was really asking "am I called `Android`?" and
`Android/data` was being walked.

### 3.6 The scan cache — 82% smaller on the user's device

The cache is by far the largest thing this app writes: ~390 KB of "app data" in
the report, several times the size of the APK. Nearly all of it is repetition,
because the list is sorted by full path:

```
/storage/emulated/0/Music/Pink Floyd/Animals/01 Pigs on the Wing.flac
/storage/emulated/0/Music/Pink Floyd/Animals/02 Dogs.flac
                                             ^ 48 identical bytes
```

Each line is now `<bytes shared with the previous line>\t<the rest>`.
**Measured on the 4,823-track stress library: 724,324 → 131,736 bytes, −81.8%.**
Nothing about the in-memory library changes; paths expand back to full strings
on load.

Safety, because this is the app's most consequential persistent state: a
version marker on the first line, and any impossible shared-prefix count,
non-numeric field or missing separator makes the whole file suspect. In every
such case — including a v1.0.0 plain-path cache — the cache is discarded and a
rescan runs. **The failure mode is "one extra scan", never "a wrong library".**
All five of those paths are covered by tests.

Writes also go to a temp file and `rename()`, so a scan interrupted by the
process being killed leaves the previous good cache intact instead of a
half-written one that looks like a tiny library.

### 3.7 `ui_main.c` — the library was held four times

The old state kept `music_all_paths`, `music_all_labels`, `music_paths`,
`music_labels` — four arrays, each a separate `strdup()` per row. Showing "all
tracks" therefore held four copies of the library.

- **Labels are gone entirely.** A track's label *is* the part of its path after
  the last `/`, so it is now a pointer into the path computed at draw time, not
  a second `strdup()` of it. That removed two of the four arrays outright.
- **"All tracks" allocates nothing.** It reads straight out of the master list.
- **One album's tracks** is an index array into the master arena — a 20-track
  album costs 160 bytes, not 20 `strdup()`s.
- **The album list** dropped a `music_all_count * 192`-byte scratch buffer (576 KB
  reserved for a 3,000-track library, to hold what is usually a couple of
  hundred short strings) and an O(albums) linear dedupe per track; it sorts once
  and compares against the previous name.
- The playback queue is handed over as one packed arena instead of one
  `strdup()` per track.
- `PlaybackSnapshot` is ~1 KB taken under the playback mutex; the old code took
  three or four per redraw. Now one per frame.
- While playing, the redraw timer wakes exactly when the `mm:ss` readout is
  about to change instead of twice a second regardless — same visible clock,
  roughly half the redraws, and a redraw is a full-screen clear plus a few
  hundred glyph blits.
- A backgrounded app no longer wakes once a second to call a `draw_frame()`
  that returns immediately.

**Verified:** the album/filter/label logic was extracted *verbatim* from
`ui_main.c` into a host harness and run under ASan/UBSan — nested, single-level,
filesystem-root and no-slash album names; unique-album counting; case-insensitive
sort; the borrowed-arena invariant (every filtered entry points inside the master
arena, no path text copied); unknown album returns empty without crashing.

### 3.8 Everything else

- `native_bridge.c`: 274 → 180 lines, ten JNI round-trips down to four.
- `proguard-rules.pro`: eight keep-rules deleted with the methods they
  protected; `-allowaccessmodification` and `-repackageclasses` added so R8 can
  merge the small Kotlin objects.
- `AndroidManifest.xml`: `appCategory="audio"`, and `extractNativeLibs="false"`
  so the installer mmaps the `.so` out of the APK instead of extracting a second
  copy into `/data/app/.../lib` — which is what actually counts against the
  user's storage.

---

## 4. Checked and ruled out — these are already at their minimum

Listing these so the next person does not spend time re-deriving them.

| Candidate | Verdict |
|---|---|
| **The font** | 95 glyphs × 16 bytes = 1,520 B of `.rodata`. Trimming the 1–3 unused bottom rows saves ~95 B and breaks the clean 16-row structure. Not worth it. |
| **`FLAC__crc16_table`** | 4,096 B of `.rodata`, the single largest constant in the binary. Genuinely used by the bitreader for frame CRC. Computing it at startup instead trades 4 KB of storage for 4 KB of *dirty* RAM, which is worse. |
| **`md5.c`** | ~4.6 KB. The decoder references `FLAC__MD5Accumulate` unconditionally, so `--gc-sections` cannot drop it even though MD5 checking is off by default. Removing it means patching upstream libFLAC — a permanent maintenance cost against the spec's contributor-friendliness goal, for 4.6 KB. |
| **`read_metadata_cuesheet_` / `_picture_` / `_vorbiscomment_`** | ~2.3 KB inside `stream_decoder.c`, reachable from `read_metadata_`, so not collectable. Same patch-upstream trade. Also: keeping the Vorbis Comment path intact is what spec 3.9.2 needs if the `TITLE` tag is ever implemented. |
| **The app icon** | Two adaptive-icon XMLs plus one vector, deduplicated by the resource optimizer to 1,024 B total, plus a 1,104 B `resources.arsc` holding one string and one colour. There is nothing left to take. |
| **Kotlin → Java rewrite** | Would remove kotlin-stdlib from the dex — worth an estimated 10–15 KB. It is a full language port of four files for ~10% of the APK, on the layer that owns the hearing-safety DAC dialog. Listed as available, not taken. See section 5. |
| **`ANativeWindow` buffer count** | 3 buffers is the compositor's decision; there is no public NDK API to ask for fewer. |
| **Rendering at half resolution and letting the compositor upscale** | Would quarter the graphics memory. It also visibly blurs the text. That is a change to what the user sees, not an optimization. |
| **`PlaybackSnapshot`'s two `char[512]`** | 1 KB of struct, but it is one static instance now, not a per-widget temporary. Shrinking the fields risks truncating real paths. |
| **Playlist file format** | Front-coding them like the scan cache would save a few KB across all playlists combined. Not worth a second format to maintain. |

---

## 5. Open decisions — these need you, not more code

Each of these is a real remaining lever. None is taken here, and the reason is
stated for each.

### 5.1 `minSdk 31` excludes roughly a third of active Android devices

This is by far the largest remaining "universality" lever, and it is a
**deliberate, published decision**, not a bug: spec section 2 sets Android 12 as
the floor and section 2.1 gives the justification. Lowering it to API 26 is
technically feasible — `PCM_I32` (API 31) would fall back to `PCM_FLOAT`, and
`StorageManager.storageVolumes` (API 30) is no longer used at all now that the
scan is in C. But it widens the AAudio/HAL test surface exactly where spec
section 5 says the fragmentation lives, and it contradicts a decision the
transparency document commits to publicly. **Your call, not a silent change.**

### 5.2 A DAC connected mid-playback does not pause playback

Spec section 3.5 says playback is blocked until the volume question is
answered, on hearing-safety grounds. In the code, `pc_set_dac_blocked(true)`
only prevents *starting* a new track — audio already playing keeps going, and
now routes to the new DAC at whatever the system volume happens to be. That is
the scenario the spec's safety argument is about.

Making it pause is a three-line change. It is **not** made here because it
changes what the app does, which was outside this pass. Flagged because it is a
safety gap, not a preference.

### 5.3 `hardwareAccelerated` was tried at `false` and reverted

The player UI would not miss it: `NativeActivity` calls `Window.takeSurface()`
and this app draws every pixel by hand, so HWUI never touches the player
surface. It looked like ~1 MB (`GL mtrack 1,156 KB`) of free graphics memory.

Reverted, because the app has one real View: the `AlertDialog` that asks
whether a new DAC has its own volume control. That dialog is a hearing-safety
gate, and forcing the whole app to software rendering puts it on a path far
fewer devices exercise. ~1 MB is not worth risking that dialog on an unknown
handset. The reasoning is recorded in the manifest so it is not re-litigated
from scratch.

### 5.4 Publishing multiple APKs

`splits.abi` produces one APK per ABI, which is what GitHub and F-Droid want.
If this ever goes to Play, build an AAB (`bundleRelease`) instead — Play does
the same split server-side and handles the per-split `versionCode` requirement
for you.

### 5.5 `fix_scan.sh` is now dead and should not ship

It is a one-shot patch script that rewrites `MusicLibrary.kt` — a file this
pass deleted — and applies fixes that are already incorporated (the resilient
per-directory scan now lives in `music_library.c`). Run today it aborts on the
first missing file without changing anything, so it is not dangerous, but
publishing a repo containing a script that patches a file which does not exist
will confuse the first contributor who tries it. Left in place rather than
deleted, because deleting your files is your call; recommend removing it before
the public release.

### 5.6 Where the verification lives

`tools/tests/run.sh` builds and runs all six suites (music library, cache
format, album views, renderer, ring buffer under ASan, ring buffer under TSan).
It needs only `gcc` and `python3` — no SDK, no NDK, no device — and adds
nothing to the APK.

Two of the suites do not test a copy of the logic: `test_album_views.c` and
`test_ring_buffer.c` `#include` a file that is extracted **verbatim** from
`ui_main.c` and `flac_stream.c` every time the runner starts, so they cannot
quietly drift away from what actually ships.

### 5.7 Everything here still needs a device

None of this has run on hardware. What has been verified is: it builds clean for
all three ABIs with `-Wall -Wextra -Wshadow` and zero warnings, and the scanner,
renderer and audio ring buffer pass targeted tests under ASan, UBSan and TSan
using the shipped source. The audio path in particular — the fallback ladder,
stream reuse between tracks, MMAP introspection via `dlsym` — is exactly the
part a host harness cannot reach. Test it on the A55 and on at least one device
that does *not* grant EXCLUSIVE, which is now a supported case rather than a
silent failure.

---

## 6. Answer to the question as asked

> *check if anything else can be reduced, ANYTHING*

Yes, and it was: **−41% APK on arm64, −82% on the cache file the 390 KB of app
data is made of, roughly −3.3 MB of projected RAM from the window buffers
alone, plus the ring buffer, the 128 KB decode stack, and the library going from
four copies to one.**

> *tell if its possible or not possible to lower digits*

Section 4 is the honest "not possible without a real cost" list — the CRC table,
MD5, the metadata readers and the icon are at their floor, and the remaining
levers (a Kotlin→Java port, a lower `minSdk`, half-resolution rendering) each
trade something the project has said it cares about. Section 5 is what is left,
and those are decisions rather than work.
