# Transparency

This document exists because the project's own specification requires it
(`FLAC_PLAYER_SPEC_v2.md` section 6). Its purpose is to state, in plain
language, exactly what this app does, what it refuses to do, what its
architecture costs, and where its central promise stops being a promise.

Where a number appears here it is either **measured** on a stated build and
device, or explicitly marked as an **estimate**. That distinction is not
decoration — publishing an estimate as if it were a measurement is the exact
failure this document is meant to prevent.

**Applies to:** version `1.0.0`.
**Measurements taken:** 2026-08-01, Samsung Galaxy A55 (SM-A556E), Android 16
(API 36), arm64-v8a, 658-track FLAC library on a microSD card, XMOS USB DAC
(VID `0x20B1`, PID `0x3021`).

---

## 1. What the app does

- Finds every `.flac` file on your phone's internal storage and on a memory
  card, by walking the filesystem directly. It does not require a folder
  called "Music", and it does not use Android's media database.
- Remembers what it found, so the next launch is instant.
- Lists your tracks by filename, or grouped by the folder they live in
  ("albums"). Folders named `Disc 1`, `CD 2` and so on are treated as part of
  the album above them rather than as albums of their own.
- Plays a track, pauses it, and moves to the next or previous one. When a
  track ends it moves to the next by itself, and stops at the end of the list.
- Lets you make playlists and add tracks to them. Playlists are plain text
  files in the app's private storage, one file path per line — readable and
  editable with any text editor if you pull them off the device.
- Keeps playing when you leave the app, with a notification, until you stop it.
- Asks, once per USB DAC, whether that DAC has its own volume control, and
  behaves differently depending on your answer (see section 4).
- Shows, continuously, whether the audio reaching your DAC is bit-perfect.
- Offers a black or white theme and four text sizes.

That is the complete list. There is nothing else in the app.

## 2. What it deliberately does not do

These are decisions. None of them is an unfinished feature or an oversight.

**No equalizer, no bass boost, no virtualizer, no DSP of any kind.** Every
processing stage between the file and the DAC is an opportunity to alter the
samples, which is the one thing this app is built not to do. If you want
tone controls, this app is the wrong tool and other players do it well.

**No internet access whatsoever.** The app does not request the `INTERNET`
permission, so the operating system will not let it open a network connection
even if it tried. This is verifiable rather than trust-based: read
`app/src/main/AndroidManifest.xml`, which is short enough to read in full.
There is no analytics, no crash reporting, no update check, no cover-art
download, no scrobbling.

**No artist, album or track-number tags.** Tracks show as filenames and group
by folder. This was evaluated and rejected on scope grounds, not cost:
displaying artist and album creates a natural expectation of browsing by
artist and album, which pushes the app toward being a conventional library
player — the thing Musicolet and Poweramp already do better. The
differentiator here is bit-perfect plus radical minimalism, and diluting the
second to compete on the first's terrain would make the project less useful,
not more.

**No album art.** Same reason, plus it would be by far the largest thing in
memory.

**No volume control inside the app.** Digital attenuation is exactly how
bit-perfect gets broken quietly. The only volume control this app will ever
use is your DAC's own, or Android's system volume — and when the system's is
in play, the indicator says so.

**No crossfade, no seek bar, no shuffle, no repeat. No true gapless
playback** — the silence between two tracks measures 7–10 ms here, which is
short but not nothing, and a listener can hear the seam on an album written
to run continuously. Sample-exact gapless would mean decoding the next track
into the same buffer before the current one ends and tracking which track
every sample in flight belongs to; that is a correctness risk this player
declines to take. Expect the figure to differ on other hardware — it is set
by storage speed and by the device's audio HAL, neither of which this project
controls.

**No settings beyond theme and text size.**

**No accounts, no cloud, no sync.**

## 3. What the architecture costs

The user interface is written entirely in C, drawing every pixel by hand onto
a raw graphics buffer. There is no Android UI framework in it — no Views, no
Compose, no layouts, no system fonts. Text is drawn by copying glyphs out of
an 8×16 bitmap font compiled into the binary.

**What that buys:** the app installs in 131 KB and runs in roughly 15–20
MB of RAM. A conventional Kotlin/Compose player of the same scope would be
tens of megabytes of APK and 30–45 MB of RAM.

**What that costs, honestly:**

- **Far fewer people can contribute.** Fixing a bug here means reading manual
  memory management, hand-written touch handling, and a custom text renderer.
  That is a much higher barrier than "edit a Compose function".
- **More low-level bugs are possible.** A whole class of mistakes the Android
  framework makes impossible — buffer overruns, use-after-free, data races
  between the audio thread and everything else — are live risks here and have
  to be prevented by hand. This is the standing cost of the approach, not a
  hypothetical one, which is why the project carries a sanitiser test suite
  (`tools/tests/run.sh`) over the scanner, the renderer and the audio ring
  buffer.
- **Accessibility is effectively absent.** Because the app draws its own
  pixels instead of using Views, Android's screen readers have nothing to
  read. TalkBack cannot describe this interface. This is a real exclusion and
  is not currently solved.
- **No automatic right-to-left layout, no system font scaling**, and no
  automatic adaptation to future Android UI conventions.

The project accepts these costs. They are stated here so that nobody adopts
the app, or contributes to it, on a false impression of what it is.

## 4. The limits of the bit-perfect promise

**The app cannot guarantee bit-perfect playback on your device, and neither
can anyone else.**

Bit-perfect requires Android to grant the app exclusive, memory-mapped access
to the audio hardware, bypassing the system mixer. Whether that is granted
depends on the audio HAL your phone's manufacturer wrote. It varies by brand,
by model, by chipset, and sometimes by which system audio features are
enabled. There is no published, maintained list of which devices support it
reliably. The only way to know about a specific phone is to try it.

Because the project cannot honestly publish a compatibility list it has not
tested, it does the next best thing: **the app reports its actual state, live,
and explains it on request.**

| Indicator | Meaning |
|---|---|
| `BIT-PERFECT: YES` | Exclusive mode granted, memory-mapped path confirmed, and the output format carries every bit of the source file. |
| `BIT-PERFECT: PARTIAL` | Exclusive mode works, but Android's software volume is below maximum and is scaling the samples before they reach the DAC. |
| `BIT-PERFECT: NO` | Any of: the HAL refused exclusive mode; output is Bluetooth; output is the phone's internal speaker or jack; the granted format cannot carry the file's bit depth; **or the channel count or sample rate had to be converted** (see below). |

**Every one of those conditions is re-checked continuously**, not recorded
once when the track opened. An indicator that kept showing `YES` after the
audio path had been taken away would be exactly the dishonesty this document
exists to prevent.

### Channel and rate conversion are not bit-perfect

If a 5.1 file is folded down to a stereo DAC, the samples reaching the DAC
are a *mix* of the file's channels, not the file's channels. The same applies
to a mono file played as stereo, and to any sample-rate conversion. This is
true no matter how good the rest of the path is, so it is reported as `NO`.

This case is not in the original specification's table of states (section
3.6). It is documented here as a real fourth way to lose bit-perfect, and the
app names it explicitly rather than blaming the device's audio driver — a 5.1
file folded into stereo is a property of the file, not a fault in the phone.

Tapping the indicator explains which case applies.

**When bit-perfect is not available, the app still plays.** It falls back to
Android's normal shared audio path and says `NO`.

### Bluetooth can never be bit-perfect

Not in this app and not in any app. Every Bluetooth audio codec re-encodes the
signal. This is a property of the protocol, not a limitation of the software.

### A known silent-failure mode

There are documented reports, from Google's own Oboe project, of Samsung/Exynos
devices producing **complete silence** — not degraded audio, silence — when
memory-mapped audio is combined with factory audio enhancements such as Dolby
Atmos. If this app shows `BIT-PERFECT: YES` and you hear nothing, disable audio
enhancements in your system sound settings before concluding the app is broken.

### What was verified, and on what

On the development device only (Galaxy A55, XMOS DAC):

- 96 kHz / 24-bit FLAC: exclusive mode granted, memory-mapped path confirmed
  by the platform, output format carrying all 24 bits → `BIT-PERFECT: YES`.
- 44.1 kHz / 16-bit FLAC: same result.
- Track changes reuse the open exclusive stream rather than renegotiating, so
  bit-perfect status does not silently degrade partway through an album.
- Zero audio underruns across the test session.

**One device and one DAC is not evidence about your device.** See
[COMPATIBILITY.md](COMPATIBILITY.md).

## 5. Permissions

| Permission | Why it is strictly necessary |
|---|---|
| `READ_MEDIA_AUDIO` (Android 13+) | To open and read your FLAC files. This is the narrowest permission that allows it — it grants audio only, not photos, video or documents. |
| `READ_EXTERNAL_STORAGE` (Android 8–12, `maxSdkVersion="32"`) | The same thing on older releases, where the audio-only permission did not exist yet. It is explicitly capped so newer Android never grants it. |
| `FOREGROUND_SERVICE` | Required by Android to run a service that continues while the app is not on screen. Without it, music stops when you leave the app. |
| `FOREGROUND_SERVICE_MEDIA_PLAYBACK` | Android 14+ requires the specific category of foreground service to be declared. Ours is media playback. |
| `POST_NOTIFICATIONS` (Android 13+) | A foreground service must display a notification, and Android requires this permission to show one. |

**`MANAGE_EXTERNAL_STORAGE` ("all files access") is deliberately not
requested.** It is not needed to read audio, it grants far more than this app
should have, and requesting it without need would contradict everything else
in this document.

**`INTERNET` is not requested.** See section 2.

`android:requestLegacyExternalStorage="true"` is set. It has an effect only on
Android 10, the single release where reading files by path was blocked without
it. It does not grant additional access on any other version.

## 6. Minimum Android version

**Android 8.0 (API 26).**

This is not a preference. AAudio — the audio API that makes bit-perfect
possible at all — was introduced in Android 8.0 and does not exist before it.
Verified by compiling the entire native layer against API 21, 23, 26, 29, 30
and 31 with unavailable-API use treated as an error: clean from 26 upward,
failing at 23 and below on the AAudio functions themselves.

Supporting older Android would require a second audio backend built on
OpenSL ES, which **cannot** deliver bit-perfect output. That would be a
different product wearing this one's name.

**This is lower than the project's design specification originally proposed.**
`FLAC_PLAYER_SPEC_v2.md` section 2 argued for Android 12, as a bet on newer
devices having more mature exclusive-mode implementations. That reasoning
assumed an immature audio driver meant no sound at all. With the fallback path
described in section 4, such a device simply gets `BIT-PERFECT: NO` and working
audio, so there is no longer a reason to exclude Android 8 through 11. The
original argument is left in the specification rather than rewritten, because
a design record that quietly edits its own history is worth nothing.

**The honest cost:** the range of Android versions this app claims to support
is now much wider than the range it has been tested on. It has been run on
Android 16. Everything from 8.0 to 15 is supported by construction and by
compiler verification, not by testing.

## 7. Measured resource use

All measured on the build and device stated at the top. **These are
measurements of one build on one device, not guarantees.**

### Storage

| | |
|---|---|
| APK, arm64-v8a | 120,722 bytes |
| APK, armeabi-v7a | 102,092 bytes |
| APK, x86_64 | 119,623 bytes |
| APK, universal (all three) | 300,015 bytes |
| Installed size on device | **131 KB** |
| Scan cache, 658 tracks | 30,045 bytes (~46 bytes per track) |
| Playlists | a few bytes per track |

### Memory

Roughly **15–20 MB** in use, varying with what you have been doing. The
largest single component is the window's graphics buffer, about 4.4 MB on a
1080×2340 screen; most of the remainder is the Android runtime itself, which
the app cannot avoid because notifications, the playback service and USB
detection all require it.

**The project's original design estimate was 8–16 MB**, which assumed 1–3 MB
for the graphics buffer. On a modern high-resolution display that estimate was
too optimistic; the real figure is 4.4 MB even after halving it by using a
16-bit surface. The measured total therefore lands at the top of the
originally estimated band and sometimes above it. That is recorded here rather
than quietly adjusted.

### CPU during playback

| | |
|---|---|
| 44.1 kHz / 16-bit, bit-perfect | 18.9% of one core |
| — of which Android's memory-mapped audio path | ~13.9% |
| — of which FLAC decoding | ~3.8% |
| — of which the user interface | ~1.0% |

On an eight-core phone, 18.9% of one core is about 2.4% of total CPU.

### What bit-perfect costs in battery — measured, not asserted

An earlier draft of this document asserted that bit-perfect costs more power
without measuring it. It does, and here is by how much. Same build, same
96 kHz/24-bit file, same 30-second window, with only the audio mode forced:

| | CPU | Context switches (wakeups) | Callback size |
|---|---|---|---|
| SHARED (not bit-perfect) | 8.80% of one core | 151/s | 2562 frames (~27 ms) |
| EXCLUSIVE (bit-perfect) | 19.96% of one core | 578/s | 192 frames (~2 ms) |

**Bit-perfect costs 2.3× the CPU and 3.8× the wakeups.**

The wakeup figure matters as much as the CPU one: each wakeup keeps the
processor out of its deep idle states, and the exclusive path is *required* to
be low-latency, so it wakes every 2 ms by design. Almost none of this is FLAC
decoding, which is about 4% either way.

This is not something the app can optimise away — it is what asking the
operating system for an untouched signal path costs. Raising the callback size
tenfold was tried and changed total CPU by 0.8%, which is measurement noise.

If battery life matters more to you than bit-perfect on a given day, a
conventional player using the shared mixer will genuinely use less power.

### Library scan

658 FLAC files across two storage volumes, discovered in **254 ms**. Scanning
runs in the background; the interface is usable while it happens.

---

## 8. Known limitations

- **The armeabi-v7a (32-bit ARM) build has never been executed by anyone.**
  It compiles cleanly for that target and passes every static check, but the
  development phone is 64-bit only and no emulator or 32-bit device was
  available, so it has never actually run. It ships anyway, labelled, because
  a 32-bit user having something to try beats having nothing — but the first
  person to run it is doing so untested. arm64-v8a is the build that has been
  tested.
- **Screen readers cannot read this app.** See section 3.
- **Supported on Android 8.0+, tested on Android 16.** See section 6.
- **Multi-disc detection is heuristic.** A folder named `Disc 1` is folded
  into its parent album. A real album genuinely called `Disc` or `CD Single`
  is not folded, but the heuristic can still be wrong for unusual layouts.
- **No seeking within a track.**
- **The library cap is 100,000 tracks.** Beyond that the scan stops and logs
  that the result is incomplete.
- **Files that are not indexed as audio by the system may be unreadable** on
  Android 10 and later, regardless of this app, because of how scoped storage
  works.

## 9. Reporting a problem

Please include: your phone model, Android version, what the bit-perfect
indicator said, and whether a USB DAC was connected. If audio failed, the
output of `adb logcat -s FlastStream:V` covers the audio path without
including anything personal.
