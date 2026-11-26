# MASTER GLUE

**English** | [日本語](README.ja.md)

An Audio FX module for [Schwung](https://github.com/charlesvestal/schwung) on
Ableton Move. It is **a clean stereo bus compressor plus a look-ahead peak
limiter, meant for the Master FX slot**.

- A stereo-linked RMS compressor (soft knee, sidechain HPF, AUTO release)
- Followed by a sample-peak limiter (1–3 ms look-ahead, the Ceiling strictly held)
- No saturation, distortion or EQ on the audio at all
- Designed to be inserted and left alone, not touched during a live set

The SSL G-Series Bus Compressor served as a reference for the parameter set,
but neither its circuit nor its behaviour is reproduced. This module has no
connection with Solid State Logic.

---

## First, what Master FX processes

In the Schwung 1.7.3 host (`src/schwung_shim.c`), the audio Master FX receives
depends on a setting.

| Setting | What Master FX processes | How MASTER GLUE acts |
|---|---|---|
| **Move→Schwung (Link Audio routing) ON** | **The whole mix**: Move's 4 tracks together with the Schwung slots | Works as the master-bus compressor it is designed to be |
| Move→Schwung OFF (normal) | **Only Schwung's audio** (slot synths, slot FX, send returns). Move's own tracks are added **after** Master FX | Move's tracks are not processed. The Ceiling applies only to the Schwung side |

**To glue Move's kick and bass, turn on Global Settings → Audio →
Move->Schwung.** Left OFF, only the Schwung side is compressed, and it is 2 ms
late (see "Latency" below).

Master FX's output is a unity-gain mix, and the master volume is applied after
it. The Ceiling is therefore relative to "full scale at maximum master volume".

## Installing

1. Get `master-glue-module.tar.gz` from Releases, or build it yourself (run
   `scripts/build.sh arm64` and it appears in `dist/`).
2. Upload it with **Install custom module** in Schwung Manager
   (`http://move.local:7700`). From the command line:

   ```bash
   python3 scripts/install.py http://move.local:7700
   ```

3. Restart Move.

## Placing it in Master FX

1. Open Master FX with **Shift + Vol + Menu** (or a long press on Menu).
2. Choose `+` at a free position and pick **MASTER GLUE**.
3. Normally put it **last** in Master FX (so the limiter comes last).
4. Turn on **Global Settings → Audio → Move->Schwung** (see the table above).
   For the host to enter this mode, track audio must be arriving from Move's
   Link Audio. While it is not, the host falls back to the normal mode by itself.

## Pages and parameters

Switch pages with the jog wheel. The main adjustments are all on the MAIN page.

### Page 1: MAIN

| Knob | Display | What it does | Range | Default |
|---|---|---|---|---|
| 1 | GLUE | Compression macro. Threshold = **-0.8 dB × GLUE%** | 0–100 % (no compression at 0 %) | 25 % (= -20 dBFS) |
| 2 | PUNCH | Attack time | 1–50 ms | 30 ms |
| 3 | REL | Release | AUTO / 50 / 100 / 150 / 200 / 300 / 400 / 600 / 800 / 1000 ms | AUTO |
| 4 | LOWP | Low Protect: a high-pass on the detector only | OFF / 40 / 60 / 80 / 90 / 100 / 120 / 150 / 200 Hz | 100 Hz |
| 5 | DENS | Density: make-up gain after the compressor (only raises the level, does not change the character) | 0 to +12 dB | 0 dB |
| 6 | LIMIT | Input gain into the limiter | 0 to +12 dB | 0 dB |
| 7 | CEIL | Limiter's maximum output | -6 to -0.3 dBFS | -1.0 dBFS |
| 8 | OUT | Final output (attenuation only) | -24 to 0 dB | 0 dB |

- **A guide to GLUE**: on a mix with peaks around -2 dBFS and RMS around
  -16 dBFS, 25 % gives about 1.4 dB of GR on average and about 3 dB at most
  (measured on a synthetic mix). 20–40 % is the everyday range, but the effect
  depends on the input level, so set it while watching COMP on the Meters page.
- **OUT only attenuates** and sits **after** the limiter, because amplifying
  after the limiter would exceed the Ceiling. To make things louder, use DENS
  or LIMIT.

### Page 2: ADVANCED

| Knob | Display | What it does | Range | Default |
|---|---|---|---|---|
| 1 | RATIO | Ratio | 1.5:1 / 2:1 / 3:1 / 4:1 | 2:1 |
| 2 | THRSH | Threshold (**the same value as GLUE, shown in dB**) | -80 to 0 dB | -20 dB |
| 3 | KNEE | Soft-knee width | 0–12 dB | 6 dB |
| 4 | L.REL | Limiter release | 20–500 ms | 100 ms |
| 5 | LOOK | Look-ahead | 1–3 ms | 2 ms |
| 6 | C.BYP | Bypass the compressor only | OFF / ON | OFF |
| 7 | L.BYP | Bypass the limiter only | OFF / ON | OFF |
| 8 | BYP | Bypass everything | OFF / ON | OFF |

**GLUE versus THRESHOLD**: the two are one value seen in different units, so
they can never contradict each other. Turning THRSH moves GLUE and turning GLUE
moves THRSH. Only GLUE is saved. THRSH at 0 dB (GLUE 0 %) switches the
compressor off completely.

### Page 3: METERS

| Display | What it shows |
|---|---|
| IN | Input peak (dBFS, floor at -60) |
| OUT | Output peak (dBFS) |
| COMP | Compressor gain reduction (dB, negative) |
| LIMIT | Limiter gain reduction (dB, negative) |
| A/B | **Level-matched comparison**. When ON, the unprocessed audio (with the same 2 ms delay) plays at the same RMS level as the processed audio |

- The meters are read-only (dotted frame) and fall at 20 dB/s after a 0.5 s peak
  hold. The DSP only records the values, so reading them on screen never stalls
  processing. They update a few times a second.
- **Using A/B**: the compressor lowers the level and DENS raises it, so an
  ordinary bypass comparison falls for "louder sounds better". A/B=ON plays the
  unprocessed audio at the processed level, so you judge the character rather
  than the level difference. The level follows an RMS over about 3 s, so right
  after switching it takes a few seconds to match.

### Page 4: PRESETS

A browser of factory presets. Choosing one with the jog applies it at once.
Save your own settings on the **My Presets** page the host adds automatically
(saving and loading the state).

## Factory presets

All have GLUE 25 %, Knee 6 dB, Ceiling -1 dBFS, DENS / LIMIT / OUT 0 dB,
L.REL 100 ms, LOOK 2 ms.

| Preset | Ratio | Attack | Release | Low Protect | Use |
|---|---|---|---|---|---|
| **TECHNO GLUE** (initial state) | 2:1 | 30 ms | AUTO | 100 Hz | Everyday techno. Keeps the kick's front and glues the rest |
| **DEEP GLUE** | 1.5:1 | 40 ms | AUTO | 120 Hz | Deep / ambient techno. Keeps the low-end tails and space |
| **TIGHT TECHNO** | 3:1 | 15 ms | 100 ms | 90 Hz | Hard / industrial. Tighter |

The preset values follow the specification. They are verified on synthetic
signals, but have not yet been tuned by ear on real mixes (see "Test results").

## Recommended settings

- Start with **TECHNO GLUE** and set GLUE so that the COMP meter reads
  **-1 to -3 dB** in the loud parts of the track.
- Then switch **A/B** on and off and judge by character, not by level.
- If the low end makes everything pump, **raise LOWP** (120–150 Hz) or set
  **REL to AUTO**.
- If the kick sounds squashed, **slow PUNCH down** (30–50 ms).
- **Leave LIMIT at 0 dB** as a rule. In normal playback the limiter should
  barely move; if the LIMIT meter swings by several dB all the time, the level is
  too high.

## Internal DSP (the actual processing order)

```
input (int16 stereo)
  ├─ sidechain: Low Protect HPF (2nd-order Butterworth, per channel, detection only)
  │     → max(L², R²) → RMS (5 ms) → dB
  │     → soft-knee gain computer (threshold, ratio, knee)
  │     → envelope (in dB, attack = PUNCH)
  │        AUTO: the deeper of a fast envelope (release 120 ms) and a slow one (charge 600 ms, release 1.2 s)
  ├─ main path: × compressor gain (shared by L/R) × DENS × LIMIT
  ├─ look-ahead limiter (gain shared by L/R)
  │     required gain = min(1, Ceiling / max(|L|,|R|))
  │     → running minimum (LOOK + 1 samples) → release (drops at once, recovers over L.REL)
  │     → moving average (LOOK samples) → applied to the audio delayed by LOOK
  ├─ × OUT (≤ 0 dB)
  ├─ A/B: the unprocessed audio with the same delay × level-matching gain (10 ms crossfade)
  └─ BYP: the input itself with no delay (10 ms crossfade)
output (int16)
```

- **Stereo link**: detection uses `max(L², R²)` and the same gain is applied to
  L and R. A sound loud on only one side does not move the image (in tests the
  L/R GR difference is 0.01 dB).
- **Limiter**: a running minimum combined with a moving average of the same
  length. This guarantees that the gain has fallen to the required value at the
  moment each peak leaves the delay (a sample-peak guarantee). A final clamp at
  the Ceiling is only there to catch rounding error and the few samples right
  after LOOK or CEIL change.
- **Click prevention**: gain, threshold and Ceiling are smoothed over 20 ms and
  the bypasses crossfade over 10 ms. A LOOK change crossfades between the old
  and new delay taps over 256 samples.
- **Safety**: if a value becomes NaN/Inf the state is reset and counted in
  `nan_resets`. Denormals are flushed to zero every block.
- **Realtime safety**: `process_block` / `set_param` / `get_param` all run on
  Move's SPI callback. There is no dynamic allocation, file I/O or locking, and
  instances are taken from a static pool with atomic operations.
  `scripts/check_abi.py` checks that malloc/mutex are not referenced.
- 44.1 kHz and 48 kHz are supported (coefficients and the look-ahead in samples
  are computed from the sample rate).

## Latency

- It is late by the look-ahead. The default 2 ms is **88 samples** (44.1 kHz),
  variable from 1 to 3 ms.
- Schwung has no latency compensation.
  - **Move→Schwung ON**: everything is delayed by the same 2 ms, so nothing
    drifts apart (Link Audio's own delay is much larger than this).
  - **Move→Schwung OFF**: only Schwung's audio is 2 ms late and drifts against
    Move's audio.
- BYP returns to "the input with no delay", so only the moment of switching gets
  a 10 ms crossfade. C.BYP, L.BYP and A/B switch while keeping the delay, so the
  timing does not change.
- The host's bypass (Mute + jog click) hands back the unprocessed audio as it
  is, so the 2 ms delay vanishes at once and can leave a small step. The
  module's own BYP is smoother.

## CPU load

| Environment | Per block (128 frames) |
|---|---|
| Development machine (x86-64, for reference) | about 6 µs (0.2 % of real time) |
| Move (Cortex-A72, measured 2026-10-20) | mean **about 38 µs** (**1.3 %** of a 2902 µs block) |

The heavy work per sample is roughly one `logf` and one `expf`; there is no
oversampling. The benchmark's worst values (a few hundred to 1700 µs) include
time when the normal-priority benchmark process was preempted by Move's
realtime threads; they are not the DSP's own load. To measure:

```bash
scp build/bench-device-arm64 ableton@move.local:/data/UserData/
ssh ableton@move.local "taskset 0x7 /data/UserData/bench-device-arm64"
```

## Tests

```bash
bash scripts/test.sh          # SCHWUNG_ROOT=<a Schwung checkout> (default ../schwung)
```

It runs four things:

- `tests/test_engine.c`: calls the real plugin entry points in 128-frame blocks
  and checks them, in both a normal build and an ASan/UBSan build.
- `tests/test_ui.mjs`: runs module.json through **Schwung's own page planner and
  validator** to check the page layout.
- The ARM64 build and ABI check: glibc ≤ 2.34, a single exported symbol, no
  malloc/mutex.
- A licence check.

### Test results (2026-10-20, development machine)

| Item | Result |
|---|---|
| Silence | The output is exactly 0 too. GR shows 0, the input meter -60 |
| Neutral settings (GLUE 0, below the Ceiling) | **Bit-identical** to the input delayed by 88 samples |
| BYP ON | **Bit-identical** to the input after the crossfade |
| Ratio (-12 dB RMS input, threshold -24, hard knee) | 1.5:1 → -16.02 (theory -16.00), 2:1 → -18.03, 3:1 → -20.04, 4:1 → -21.05 dB |
| Below the threshold | Stays at -30.00 dB (no change) |
| 6 dB soft knee (exactly at the threshold) | -24.39 dB (theory -24.38) |
| Attack (time to reach 63 % of GR) | 1 / 10 / 30 / 50 ms → 2.0 / 12.0 / 31.9 / 51.9 ms |
| Release (time for GR to recover to 10 %) | 100 ms → 240 ms, 600 ms → 1392 ms |
| AUTO release | 287 ms after a 50 ms hit, 2761 ms after 3 s of compression (follows how the sound continues) |
| Low Protect (45 Hz sine at -10 dB) | GR 10.3 dB with OFF, 3.2 dB at 100 Hz, 0.0 dB at 200 Hz. The audio's 45 Hz changes by 0.000 dB |
| Stereo link (L 20 dB louder than R) | GR L -16.96 dB, R -16.95 dB |
| Ceiling (a full-scale mix in with LIMIT +12 dB and DENS +12 dB) | Never exceeds -0.3 / -1 / -6 dBFS |
| Impulse (full scale + 12 dB) | At or below -1 dBFS |
| A mix peaking at -2 dBFS with default settings | Limiter GR 0.00 dB (does nothing) |
| Sudden parameter changes (18 kinds changed in a row) | The largest sample-to-sample change is 1.07 times the sine's own slope (no clicks) |
| The 3 presets | Match the specified values |
| Saving and restoring state | Every parameter matches. Broken state strings and invalid values are ignored, out-of-range values are clamped |
| A/B level matching | -15.43 dB against -15.48 dB RMS processed |
| Long run with excessive input (10 minutes of full-scale noise, random parameter changes every block) | 0 NaN resets, 0 Ceiling overshoots, returns to exactly 0 after silence |

### A / B / C comparison (16 s synthetic techno mix, TECHNO GLUE defaults, first 4 s excluded)

The mix is a four-on-the-floor kick (160→48 Hz), an off-beat bass (55 Hz), a
303-style 1/16 line, hats and a noise texture.

| Input | Condition | Peak | RMS | Crest | GR |
|---|---|---|---|---|---|
| ×0.55 (peak -1.9 dBFS) | A bypass | -1.85 dBFS | -15.83 dBFS | 13.98 dB | — |
| | B compressor only | -2.80 | -17.45 | 14.65 | mean 1.38, max 2.95 dB |
| | C compressor + limiter | -2.80 | -17.45 | 14.65 | limiter 0.00 dB |
| ×0.9 (just short of clipping) | A bypass | 0.00 | -11.56 | 11.56 | — |
| | B compressor only | -1.87 | -14.54 | 12.67 | mean 2.70, max 4.70 dB |
| | C compressor + limiter | -1.87 | -14.54 | 12.67 | limiter 0.00 dB |

The 30 ms attack lets the kick's onset through, so after the compressor the
crest factor actually **rises** a little: the transients are not being crushed.
B and C are quieter, so use A/B (level-matched) when comparing by ear.

**Listening evaluation is not done.** All verification so far uses synthetic
signals. Listening on real techno mixes and tuning the preset values have to be
done on the hardware.

## Known limitations

- **With Move→Schwung OFF, Move's own audio is not processed** (how the host works).
- **True peak is not guaranteed.** Only the sample peak is. Inter-sample peaks
  can slightly exceed the Ceiling (normally not a problem with the Ceiling at
  -1 dBFS or below).
- A/B level matching is a plain RMS (about 3 s), not LUFS. And if raising the
  unprocessed audio goes past full scale, it clips in int16 (only during A/B).
- The Ceiling is relative to the unity-gain mix. The final DAC output also has
  the master volume applied.
- The PRESETS page applies a preset the moment it is chosen with the jog.
- The limiter's release is a fixed time (no signal-dependent automatic release).
- Listening on the hardware and tuning the presets on real mixes are still to do.

## Licence

MIT License (`LICENSE`). Copyright (c) 2026 Hiro Ikezawa.

All the DSP was written in this repository; no external DSP code is used. The
only bundled third-party file is Schwung's plugin API header (MIT). See
`THIRD_PARTY_LICENSES.md` for details.
