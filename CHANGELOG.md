# Changelog

## 1.0.0 — first public release

Everything before this was internal iteration that never left the development
machine, so the public history starts here.

Verified on a Samsung Galaxy A55 (SM-A556E), Android 16, with a 658-track FLAC
library on a microSD card and an XMOS USB DAC (`20B1:3021`).

### What it is

A bit-perfect FLAC player. 127 KB installed, ~15-20 MB of RAM, no network
access, no dependencies. The user interface is written in C and draws every
pixel itself; the Android layer is only what the OS makes unavoidable
(foreground service, notification, USB detection).

### Audio - verified on hardware

- 96 kHz/24-bit and 44.1 kHz/16-bit both reach `BIT-PERFECT: YES` - AAudio
  `EXCLUSIVE` granted, MMAP confirmed by the platform, and the stream format
  carrying every bit of the source.
- A five-rung fallback ladder (EXCLUSIVE->SHARED, I32/I16/FLOAT) means a device
  that refuses exclusive mode still **plays**, and honestly reports `NO`.
- The stream's actual sample rate and channel count are verified after opening,
  so an exclusive stream that came back at the wrong rate is rejected rather
  than playing an album at the wrong pitch.
- Bit-perfect reporting accounts for stream bit depth: a 16-bit stream carrying
  a 24-bit file is not reported as bit-perfect.
- Track changes reuse the open exclusive stream, so bit-perfect does not
  silently degrade partway through an album.
- Zero audio underruns across all testing.

### What bit-perfect costs - measured

Same build, same file, only the audio mode forced:

| | CPU | Wakeups |
|---|---|---|
| SHARED (not bit-perfect) | 8.80% of a core | 151/s |
| EXCLUSIVE (bit-perfect) | 19.96% of a core | 578/s |

**2.3x the CPU and 3.8x the wakeups.** Almost none of it is FLAC decoding,
which is about 4% either way - it is Android's low-latency exclusive path,
which wakes every 2 ms by design.

### Universality

- Ships armeabi-v7a, arm64-v8a, x86_64 and a universal APK. **armeabi-v7a has
  never been executed by anyone** - see README and TRANSPARENCY.md section 8.
- `minSdk 26` (Android 8.0), which is AAudio's own floor. Verified by compiling
  the native layer against API 21/23/26/29/30/31 with unavailable-API use as an
  error: clean from 26 up.
- `android.hardware.usb.host` is `required="false"`, so the app is not filtered
  off devices without USB OTG.
- Storage volumes come from the framework, so removable SD cards are found -
  `/storage` is not listable by apps, which made a pure-C scan miss 658 files
  on the development device.
- `requestLegacyExternalStorage` for Android 10, the one release where direct
  path reads are otherwise blocked.

### Transport, device changes and state (found by manual testing)

The final round of testing was done by hand rather than by script, and it
found four things no automated pass had:

- **Play/pause did nothing after plugging in a DAC.** Changing the audio
  device kills the open stream, and `flast_stream_resume()` called
  `AAudioStream_requestStart()` while discarding the result — so it failed
  silently and the button label toggled over silence. Resume now detects a
  dead stream (`AAUDIO_ERROR_DISCONNECTED`), reopens **only the output**, and
  returns success or failure so the UI cannot lie about it.
- **The timestamp reset to 00:00 when the DAC was unplugged.** The safety
  cutoff called `flast_stream_stop()`, tearing down the decoder and resetting
  the play position. Both plugging *and* unplugging now **pause** instead.
  Silence is still guaranteed — which is the entire point when system volume
  is pinned to maximum for a DAC that is gone — but the decoder, its position
  and the ring buffer stay alive, so play resumes exactly where it stopped.
  Costs ~250–320 KB held while paused that used to be freed; a deliberate
  trade for instant, in-place resume.
- **The bit-perfect indicator could not stop saying YES.** It was a variable
  written once when the track opened and never re-evaluated, so it kept
  claiming success after another app took the audio path. It is now queried
  live every frame: sharing mode, MMAP state, disconnect state, format, and
  sample-rate/channel match.
- **The app forgot which screen you were on.** Back-press and swipe-away
  destroy the Activity while the process keeps playing, which restarts
  `android_main()` and reset the screen to MUSIC. The last screen is now held
  in process-scoped state and restored. A full kill from the recents list
  still starts at MUSIC, which is correct for a genuinely cold start.

**One regression was introduced and caught in the same session.** Making the
pause fire on every DAC-attach callback meant reopening the app paused the
music, because Android also delivers that callback on Activity creation — and
the Activity is created on every back-press. The current DAC is now tracked in
process-scoped state, so only a genuinely different or newly-present device
counts as a device change.

### Channel and rate conversion are not bit-perfect

Playing an 88.2 kHz 5.1 file on a two-channel DAC exposed a state the original
specification's table never listed. A file folded down from 6 channels to 2 is
not the file's samples any more, and neither is one resampled from 44.1 kHz to
48 kHz. Any up-mix, down-mix or resample now disqualifies bit-perfect
explicitly, rather than happening to report correctly for another reason.

### Verified end to end on device

- **First run**: install -> permission prompts -> the pre-permission scan finds
  nothing -> granting permission triggers a rescan -> 658 files in 254 ms.
- **Denying permission**: no crash, and the empty-library message names the
  denied permission and where to grant it, instead of saying
  "no .flac files found" and blaming the user's music collection.
- **Device changes**: plugging and unplugging a DAC mid-playback pauses with
  the position kept, and pressing play reopens the output and resumes from the
  same point — confirmed on hardware, including the
  `AAUDIO_ERROR_DISCONNECTED` recovery path.
- **Reopening the app** during playback (back-press, then relaunch) does not
  interrupt the music and returns to the screen you were on.
- **New DAC**: question screen appears, answer persists per vendor/product ID,
  and a reconnect applies the saved answer without asking again.
- **DAC unplugged mid-playback**: playback stops for safety, because system
  volume was pinned to maximum for a DAC that is no longer there.
- **Playlists**: create -> add -> list -> open -> play (bit-perfect) -> delete.
- **Natural end of track** auto-advances (0.38 s); rapid NEXT/PREV/pause,
  rotation, backgrounding and fast scrolling all survive.
- **Fuzzing**: 3000 random events via Android `monkey`, no crash.
- **Leak check**: three identical stress cycles - native heap steps up once for
  the ring buffer then plateaus. Bounded.

### Bugs found and fixed during development

Each of these was real, reproduced, and is the kind of defect this
architecture invites:

- **Tracks skipped by themselves every ~10 s.** An *aborted* decode signalled
  end-of-track just like a natural one, so the watcher woke against the track
  that had just started, timed out, and advanced anyway - forever.
- **Replugging a DAC killed the next track.** The retained AAudioStream is dead
  after the device disappears; reusing it failed with
  `AAUDIO_ERROR_INVALID_STATE`. Now there is an AAudio error callback, a
  disconnect check before reuse, and a retry that reopens instead of dropping
  the track.
- **SD cards were invisible.** `opendir("/storage")` fails on a real device
  (`drwx--x--x`: traversable, not listable). 658 files were missed.
- **Use-after-free on the audio ring buffer** - it could be freed while the
  AAudio callback was still reading it.
- **A lost thread signal** that permanently broke auto-advance after a short
  track.
- **The app refused to play at all** on any device without MMAP, instead of
  falling back to SHARED.
- **Two undocumented AAudio symbols were hard-linked**, so the whole native
  library would fail to load on any device that does not export them. Resolved
  with `dlsym` now.
- **Undefined behaviour**: left-shifting a negative `int32_t` on every sample.

### Known limitations

- armeabi-v7a untested (above).
- Tested on Android 16 only, though supported from 8.0.
- Screen readers cannot read the app - it draws its own pixels, so TalkBack has
  nothing to describe. This is a real exclusion with no current solution.
- No seeking, no gapless, no metadata, no EQ, no network. All deliberate; see
  TRANSPARENCY.md section 2.

### Licence

GPL-3.0-or-later. Bundles libFLAC 1.4.3 (decoder only), BSD-3-Clause,
(C) Xiph.Org Foundation.
