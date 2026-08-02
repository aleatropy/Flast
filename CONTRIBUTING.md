# Contributing

Contributions are welcome. Before you spend time on one, please read this —
**this project rejects features on purpose**, and it would be unfair to let you
write code that was never going to be merged.

## The thing to understand first

Flast exists to do exactly one thing: get the bytes of a FLAC file to a USB
DAC without altering them, and be honest about whether it managed. Everything
else is weight.

The working rule from the project's specification:

> Every line of code that does not directly serve "decode FLAC → deliver
> untouched PCM to the DAC → basic transport controls" is a candidate for
> deletion.

So the most valuable contributions are usually **bug reports from devices we
cannot test on**, not features.

## What will not be merged

Not because they are bad ideas — because they are not this app:

- Equalizer, bass boost, replay gain, crossfade, or any other DSP. Each one is
  a chance to break the only thing the app promises.
- Artist/album/track-number metadata browsing. Evaluated and rejected on scope
  grounds; see [TRANSPARENCY.md § 2](TRANSPARENCY.md#2-what-it-deliberately-does-not-do).
- Album art.
- Any network feature at all — scrobbling, cover download, update checks. The
  app holds no `INTERNET` permission and that is a promise to users, not an
  oversight.
- Analytics, crash reporting, telemetry.
- A UI framework. The C rendering layer is the reason the app is 127 KB.
- Support for other formats (MP3, Ogg, WAV, ALAC).
- A software volume slider inside the app.

If you want any of these, forking is genuinely a reasonable answer, and the
GPL exists precisely so you can.

## What is very welcome

- **Compatibility reports.** See [COMPATIBILITY.md](COMPATIBILITY.md), or open
  an [issue](https://github.com/aleatropy/Flast/issues). A `NO`
  report from a device we have never seen is worth more than most code.
- **Bug fixes**, especially in the native layer.
- **Accessibility.** The app draws its own pixels, so screen readers cannot
  see it. This is a real exclusion with no current solution. A workable design
  would be a significant contribution.
- **Testing on Android 8 through 15.** The app supports API 26+ but has only
  been run on Android 16.
- Making the build reproducible.
- Documentation corrections — especially anywhere this project overstates what
  it has verified.

## Ground rules for code

**Measure, don't assume.** Intuition about where this app spends memory and
CPU is unreliable — the largest single consumer is the window's graphics
buffer, and most of the CPU during playback belongs to Android's low-latency
audio path rather than to FLAC decoding. If you claim an optimisation, include
the before and after numbers.

**A silent bug is the enemy.** The dangerous defects here do not crash — the
scanner quietly misses a folder, the renderer quietly writes past a row, the
audio path quietly stops being bit-perfect on track two. If your change touches
the scanner, the renderer, or the ring buffer, add a test.

**Run the tests.** They need only `gcc` and `python3` — no SDK, no device:

```
./tools/tests/run.sh
```

Two of the suites extract their functions *verbatim* from `ui_main.c` and
`flac_stream.c` at runtime, so they cannot drift from what ships. Everything
runs under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer.

**Build clean for every ABI.** The project ships armeabi-v7a, arm64-v8a and
x86_64. `./gradlew assembleRelease` builds all three; none may warn.

**Comment the why, not the what.** The code says what it does. Comments should
say why it is not the obvious thing — what broke, what was measured, what the
trade-off was. Look at the existing ones for the tone.

**Do not add dependencies.** The app currently has zero. That is why R8 has
nothing to shrink and why the APK is what it is.

## Real-time audio rules

The AAudio data callback runs on a real-time thread. Inside it, or anything it
calls:

- No allocation, no locks, no file or network I/O, no logging.
- The ring buffer between the decoder and the callback is lock-free
  single-producer/single-consumer. Keep it that way: a mutex there lets the
  real-time thread block on the decode thread, which is how dropouts happen.
- Never free anything the callback can still touch without stopping the stream
  and waiting for it first.

## Building

JDK 17, Android SDK, NDK 27.0.12077973.

```
./gradlew assembleRelease
```

Release APKs land unsigned unless you supply your own key: copy
`keystore/keystore.properties.example` to `keystore/keystore.properties`. The
project's own release key is not in this repository.

## Licence

By contributing you agree your work is licensed under **GPL-3.0-or-later**,
the same as the rest of the project. No copyright assignment is asked for; you
keep your copyright.
