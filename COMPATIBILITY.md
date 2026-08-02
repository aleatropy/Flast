# Device compatibility

**This file is built from user reports. It is not a promise, and it is not
maintained by testing.**

Whether Flast achieves bit-perfect output depends on the audio HAL your
phone's manufacturer wrote — see
[TRANSPARENCY.md § 4](TRANSPARENCY.md#4-the-limits-of-the-bit-perfect-promise).
There is no public list of which Android devices grant exclusive,
memory-mapped audio, and this project is too small to buy and test phones. The
only honest source of that information is the people using the app.

**So: if you try Flast, please add a line — even if it didn't work.** A `NO`
report is more useful than a `YES`, because it is the case nobody documents.

---

## Reports

| Device | Android | DAC (VID:PID) | Indicator | Notes | Reporter |
|---|---|---|---|---|---|
| Samsung Galaxy A55 (SM-A556E) | 16 | XMOS `20B1:3021` | `YES` | 96/24 and 44.1/16 both exclusive + MMAP confirmed | project |
| Samsung Galaxy A55 (SM-A556E) | 16 | *(none — phone output)* | `NO` | expected: no USB DAC means no bit-perfect path | project |
| *any 32-bit ARM device* | — | — | **unknown** | **armeabi-v7a has never been executed by anyone — first reports especially wanted** | — |

---

## How to add a report

Open an [issue](https://github.com/aleatropy/Flast/issues) titled
`Compatibility: <your device>`, or a pull request editing the table above. Please include:

1. **Device model** as it appears in Settings → About phone.
2. **Android version.**
3. **Your DAC**, and its vendor/product ID if you know it. Flast logs it —
   `adb logcat -s FlastDacPrefs:V` prints `getSavedAnswer(<vendor>, <product>)`.
4. **What the indicator said** — `YES`, `PARTIAL` or `NO`. Tap it; it explains
   which case applies.
5. **Whether audio actually played**, which is a separate question from
   whether it was bit-perfect.

If it failed in an interesting way, `adb logcat -s FlastStream:V` shows the
whole audio negotiation and contains nothing personal — no filenames, no paths.

## How reports are verified

They are not, beyond plausibility. Nobody can reproduce your device remotely.
Reports are recorded as *reports*, attributed to whoever made them, and this
file says so at the top rather than implying a level of confidence it does not
have.

Two things make a report much more trustworthy, so please include them if you
can:

- The exact indicator text rather than a paraphrase.
- The `FlastStream` log line beginning `AAudio open OK:`, which states the
  sharing mode, format and sample rate the device actually granted.

## Patterns worth knowing

**Bluetooth is always `NO`.** Every Bluetooth codec re-encodes audio. This is
the protocol, not the app, and no player can do better.

**Phone speaker or headphone jack is always `NO`.** Bit-perfect here means
delivering the file's exact samples to an external DAC; the internal path goes
through the phone's own conversion.

**Samsung/Exynos with audio enhancements can produce complete silence.** There
are reports, from Google's Oboe project, of memory-mapped audio plus Dolby
Atmos causing no sound at all rather than degraded sound. If the indicator says
`YES` but you hear nothing, turn off audio enhancements in your system sound
settings before filing a bug.

**A device can say `NO` and still be fine.** The app plays either way. `NO`
means "I could not get the untouched path, and I am telling you" — not "this
is broken".
