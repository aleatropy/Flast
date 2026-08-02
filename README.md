# Flast

**https://github.com/aleatropy/Flast**

A bit-perfect FLAC player for Android. **127 KB** installed, no
network access, no ads, no accounts, no library scraping, no settings you
have to understand before it plays music.

It does one thing: decode a FLAC file and hand the samples to your USB DAC
without touching a single bit on the way. It tells you, on screen, whether
it actually managed that — and when it didn't, why not.

```
MUSIC          PLAYLISTS          CONFIG
──────────────────────────────────────────
ALL TRACKS  │  ALBUMS
──────────────────────────────────────────
01. Come On, Come On.flac
02. White Wedding (Pt. 1).flac
03. Hot In The City (Single Version).flac
...
```

---

## Download and install

**If you don't know what CPU your phone has, take `app-universal-release.apk`.**
It works on every Android phone and tablet and costs you about 180 KB more.

There are **three different CPU architectures** in the Android world, not
just "64-bit and 32-bit" — the split is also **ARM vs Intel**. Phones and
tablets are ARM; emulators and a few Chromebooks are Intel.

| File | Which CPU | Which devices |
|---|---|---|
| `flast-1.0.0-universal.apk` | **all three, bundled** | **anything — pick this if unsure.** 297,703 B |
| `flast-1.0.0-arm64-v8a.apk` | 64-bit ARM | almost every phone/tablet since ~2017. 118,426 B |
| `flast-1.0.0-armeabi-v7a.apk` | 32-bit ARM | older and budget phones. **Untested — see below.** 99,360 B |
| `flast-1.0.0-x86_64.apk` | 64-bit Intel/AMD | Android emulators, some Chromebooks, a few old Intel tablets. 117,311 B |

**The universal APK is not a clever binary that runs everywhere.** It is a
bundle containing all three, and your device picks the one it needs and
ignores the other two. That is why it is roughly the size of the three added
together. It costs about 176 KB of extra storage and **zero extra RAM** —
Android never loads the copies your CPU cannot run. If you are not certain
what is inside your phone, take it; you cannot choose wrong.

1. Download the APK from the
   **[Releases page](https://github.com/aleatropy/Flast/releases)**.
2. Open it. Android will ask you to allow installing from this source — that
   prompt is normal for any app not from the Play Store.
3. Open Flast. It asks for permission to read audio files. Allow it, or the
   music list stays empty.
4. That's it. It scans your storage in the background and the list fills in.

> ### ⚠️ armeabi-v7a has never been run
>
> The 32-bit ARM build compiles cleanly and passes every static check, but
> **no one has ever executed it.** The development phone is 64-bit only, so
> there was no way to test it here. It is published because a 32-bit user
> having *something* to try is better than nothing — but you are the first
> person to run that binary, and it may not work. Please
> [report what happens](COMPATIBILITY.md) either way.
>
> The arm64-v8a, x86_64 and universal builds are unaffected by this.
> arm64-v8a is the one that has actually been tested.

**Requires Android 8.0 (API 26) or newer.** See
[TRANSPARENCY.md](TRANSPARENCY.md#6-minimum-android-version) for why that
floor exists and why it can't go lower.

### Verifying what you downloaded

Every release is signed with the same key. You can check that an APK really
came from this project:

```
apksigner verify --print-certs app-universal-release.apk
```

The SHA-256 of the signing certificate must be:

```
743e01c075419ade8c953b83f720e4bdadc9fb8310cbae0ef7b3e2ccc22d084d
```

If it differs, someone else built that APK. Don't install it.

---

## About the bit-perfect promise

**Read this before deciding whether this app is for you.** It is the most
important thing on this page.

Flast asks Android for the most direct audio path that exists — AAudio in
`EXCLUSIVE` mode with an MMAP buffer, which bypasses the system mixer
entirely. When it gets that, the exact bytes in your FLAC file reach your DAC
with no resampling, no mixing, and no volume scaling.

**Whether it gets that is not up to this app. It is up to your phone.**

Every Android manufacturer writes their own audio HAL, and whether it grants
exclusive mode varies by phone model, by chipset, and sometimes by which
system audio effects you have switched on. There is no public list of which
devices work. Nobody — not us, not Google — can tell you in advance whether
yours will.

So Flast does the only honest thing available: **it tells you, live, on the
player screen.**

| What you see | What it means |
|---|---|
| `BIT-PERFECT: YES` | Exclusive mode granted, MMAP confirmed, and the stream format carries every bit of your file. The audio is untouched. |
| `BIT-PERFECT: PARTIAL` | Exclusive mode works, but Android's software volume is below maximum and is scaling the samples. Raise the volume to max, or use a DAC with its own volume control. |
| `BIT-PERFECT: NO` | Something in the chain rules it out — see below. |

The indicator is **re-checked continuously**, not decided once when the track
started. If something takes the exclusive audio path away mid-track, it
changes to `NO` while you watch.

Tap the indicator and it tells you exactly which of those applies and why.

**`NO` is not a bug.** It means one of:

- your phone's HAL refused exclusive mode (the most common reason);
- you're on Bluetooth, which re-encodes audio by definition and can never be
  bit-perfect in any app;
- you're on the phone's own speaker or headphone jack rather than a USB DAC;
- the file is deeper than the stream format the device would grant;
- **the file's channels or sample rate had to be converted.** A 5.1 file
  folded down to a stereo DAC is not the file's samples any more, and neither
  is 44.1 kHz resampled to 48 kHz. Any up-mix, down-mix or resample means
  `NO`, however good the rest of the chain is.

**Flast still plays in all of those cases.** It falls back to Android's shared
audio path and keeps working — it just refuses to claim something it didn't
achieve.

### One known failure mode worth knowing about

On some Samsung/Exynos devices, MMAP mode combined with factory audio
enhancements (Dolby Atmos and similar) has been reported to produce **complete
silence** rather than degraded audio. If Flast shows `BIT-PERFECT: YES` but you
hear nothing, turn off audio enhancements in your system sound settings before
assuming the app is broken.

---

## What it does

- Plays FLAC. Nothing else — no MP3, no Ogg, no WAV.
- Scans **all** your storage, internal and SD card, for `.flac` files.
  It does not require a folder called "Music".
- Browses by track or by album folder. Multi-disc sets (`Disc 1`, `CD 2`, …)
  collapse into the one album they belong to.
- Playlists, stored as plain text files you can read and edit yourself.
- Play / pause / next / previous. Long-press a track to add it to a playlist.
- Keeps playing in the background with a notification.
- Two themes (black or white) and four font sizes.

## What it deliberately does not do

Each of these is a decision, not a missing feature:

- **No equalizer, no DSP, no "enhancements".** Every processing stage is a
  chance to break bit-perfect. If you want EQ, this is not your app — and
  that's fine, Poweramp and Neutron are good at it.
- **No internet access.** The app holds no `INTERNET` permission at all. You
  can verify that in [`AndroidManifest.xml`](app/src/main/AndroidManifest.xml)
  — there is nothing to trust us about.
- **No album art, no artist/album tags.** Tracks are shown by filename and
  grouped by folder. See
  [TRANSPARENCY.md](TRANSPARENCY.md#2-what-it-deliberately-does-not-do).
- **No software volume control inside the app.** Attenuating digitally would
  silently break the thing the app exists for.
- **No gapless playback, no crossfade, no seek bar.**
- **No accounts, no telemetry, no analytics, no crash reporting.**

## Permissions, and why each one exists

| Permission | Why |
|---|---|
| `READ_MEDIA_AUDIO` (Android 13+)<br>`READ_EXTERNAL_STORAGE` (Android 8–12) | To find and read your FLAC files. Without it the library is empty. |
| `FOREGROUND_SERVICE`<br>`FOREGROUND_SERVICE_MEDIA_PLAYBACK` | So music keeps playing when you leave the app. Android requires a foreground service for this. |
| `POST_NOTIFICATIONS` (Android 13+) | The foreground service must show a notification. Android requires the permission to display it. |

That is the complete list. No internet, no location, no contacts, no "all
files access".

---

## Measured, on real hardware

From a Galaxy A55 (Android 16) with a 658-track library on an SD card and an
XMOS USB DAC. These are measurements, not estimates:

| | |
|---|---|
| APK | 118,426 B (arm64) · 99,360 B (armeabi-v7a) · 297,703 B (universal) |
| Installed on device | **127 KB** |
| Scan cache for 658 tracks | 30,045 B |
| RAM in use | ~15–20 MB |
| CPU, 44.1 kHz/16-bit bit-perfect | 18.9% of one core (≈2.4% of an 8-core phone) |
| CPU, 96 kHz/24-bit bit-perfect | 19.96% of one core |
| CPU, same file in non-bit-perfect SHARED mode | 8.80% of one core |
| Library scan | 658 files across 2 volumes in 254 ms |

Roughly 14 of those 19 CPU points are Android's own low-latency MMAP audio
path, not FLAC decoding — decoding is about 4%.

**Bit-perfect costs battery, and here is the measurement rather than a
hand-wave.** Same build, same 96/24 file, only the audio mode forced:

| | CPU | Wakeups |
|---|---|---|
| SHARED (not bit-perfect) | 8.80% of a core | 151/s |
| EXCLUSIVE (bit-perfect) | 19.96% of a core | 578/s |

**2.3× the CPU and 3.8× the wakeups.** That is the real price of an untouched
signal path, and it is not something the app can optimise away — the exclusive
path is required to be low-latency, which means waking hundreds of times a
second by design. See
[TRANSPARENCY.md](TRANSPARENCY.md#7-measured-resource-use).

## Does it work on your phone?

We can't test every device, so [COMPATIBILITY.md](COMPATIBILITY.md) is
community-maintained. If you try Flast, please add a line — a `NO` report is
just as useful as a `YES`.

---

## Building from source

Needs JDK 17, the Android SDK, and NDK 27.0.12077973.

```
git clone https://github.com/aleatropy/Flast.git
cd flast
./gradlew assembleRelease
```

Unsigned APKs land in `app/build/outputs/apk/release/`. To sign them, copy
`keystore/keystore.properties.example` to `keystore/keystore.properties` and
fill in your own key — the project's release key is not in this repository and
never will be.

There is a host test suite for the parts where a bug would be silent — the
storage scanner, the renderer, and the lock-free audio ring buffer. It needs
only `gcc` and `python3`, no SDK and no device:

```
./tools/tests/run.sh
```

It compiles the *shipped* source (two suites extract their functions verbatim
from `ui_main.c` and `flac_stream.c` at runtime) and runs it under
AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer.

See [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request — this
project rejects features on purpose, and it's only fair to say so up front.

---

## Licence

Flast is **GPL-3.0-or-later**. See [LICENSE](LICENSE).

In short: you may use, study, modify and redistribute it, and anything you
distribute that is built from this code must also be free software under the
same terms.

It bundles **libFLAC 1.4.3** (decoder only), which is BSD-3-Clause from the
Xiph.Org Foundation — see
[`COPYING.Xiph`](app/src/main/cpp/third_party/flac-1.4.3/COPYING.Xiph). That
licence is compatible with the GPL, and libFLAC's copyright notices are
retained in this repository as it requires.

The embedded bitmap font is an 8×16 monospace glyph set generated at build
preparation time from a terminal font; see
[`ui_font_spleen8x16.c`](app/src/main/cpp/ui_font_spleen8x16.c).
