# Changelog

## 1.0.0

First release.

A bit-perfect FLAC player for Android: 127 KB installed, roughly 15–20 MB of
RAM, no network access and no dependencies. The interface is written in C and
draws every pixel itself; the Android layer is only what the operating system
makes unavoidable — a foreground service, its notification, and USB device
detection.

### Audio

- AAudio in `EXCLUSIVE` mode with an MMAP buffer, so the file's samples reach
  a USB DAC without resampling, mixing or volume scaling.
- A five-rung fallback ladder (`EXCLUSIVE`→`SHARED`, `PCM_I32`/`I16`/`FLOAT`).
  A device that will not grant exclusive mode still **plays**, and reports
  `BIT-PERFECT: NO` rather than failing.
- The stream's actual sample rate and channel count are verified after opening,
  so a stream granted at the wrong rate is rejected instead of playing an album
  at the wrong pitch.
- Bit-perfect is evaluated **continuously**, not once when a track starts, and
  covers sharing mode, MMAP state, output format bit depth, sample rate and
  channel layout. Any resample, up-mix or down-mix reports `NO` and says so.
- Track changes reuse the open stream, so the indicator does not silently
  degrade partway through an album.
- Audio device changes (a DAC plugged in or pulled out) pause playback with the
  position kept, and resume from the same point.

### Library

- Recursive scan of internal storage and any memory card, in C via
  `opendir`/`readdir`. No dependence on a folder named "Music" and no use of
  the media database.
- Results cached between sessions; 658 tracks reload in under a millisecond.
- Browse by track or by album folder. `Disc 1`, `CD 2` and similar folders fold
  into the album above them.
- Playlists as plain text files, one path per line.

### Interface

- Two themes, four text sizes, and a bit-perfect indicator that explains itself
  when tapped.
- Playback continues in the background and survives leaving the app.

### Platform

- Android 8.0 (API 26) and above — AAudio's own floor.
- armeabi-v7a, arm64-v8a, x86_64, and a universal APK containing all three.
  **armeabi-v7a has never been executed on hardware**; see README.
- Permissions: audio read access, foreground service, and notifications.
  No internet permission.

### Licence

GPL-3.0-or-later. Bundles libFLAC 1.4.3 (decoder only), BSD-3-Clause,
(C) Xiph.Org Foundation.
