# 🧪 The Secret Sauce

**The one plugin that makes a mix sound finished — without you having to know exactly why.**

The Secret Sauce is a "finisher" audio plugin: about twenty quiet processors — tape, tube,
transformer, console colour, parallel and upward compression, transient shaping, musical EQ,
stereo width with a seatbelt, low-end tightening, harmonic bass, soft clipping and a lookahead
limiter — each doing a tiny amount, orchestrated by one big knob.

```
                THE SECRET SAUCE

                     🧪

                  [ SAUCE ]
                   0–100%

            Warmth        Air
            Punch         Width
            Density       Shine

              [ SECRET RECIPE ]
             Vintage ○ Modern

              Mix      Output
                  Bypass
```

## The point of view

Every decision in this plugin follows one rule: **colour is free, loudness is a lie.**
Auto-gain keeps A/B comparisons loudness-matched, Delta lets you hear exactly what is being
added, and nothing in the recipe buys "better" with "louder". If it still sounds better
matched — it *is* better.

## What's inside

| Section | What you touch | What actually happens |
|---|---|---|
| **Sauce** | One knob, 0–100 % | Raises ~20 processors together on perceptual tapers, scaled by the detected source type |
| **Intelligent analog colour** | Heat, Console (Gold/Silver/Crimson/Emerald/Titanium/Copper), Recipe (Vintage/Modern) | Tape + tube + transformer + soft clip stages, ADAA anti-aliased, running oversampled; envelope-driven dynamic harmonics |
| **Micro dynamics** | Glue, Punch, Density, Snap | Parallel bus compression, upward compression, two-band transient shaping, adaptive release everywhere |
| **Frequency magic** | Warmth, Air, Body, Presence, Weight, Sparkle | Six macro controls over analog-matched (decramped) filter banks; each knob moves multiple bands |
| **Stereo** | Width, Mono Safe, Bass Mono | M/S width with energy compensation, side sheen, bass-mono anchoring, and a slow S/M ratio guard that makes mono collapse impossible |
| **Low-end tightener** | Tight | Dynamic low-band control, harmonic bass enhancement, low-frequency phase alignment, rumble guard |
| **Loudness** | Wow, Ceiling | Soft clipper + lookahead limiter with transient preservation and an inter-sample peak guard |
| **AI mix detection** | Program (Auto or forced) | An honest feature heuristic (crest, spectral balance, onsets, stereo) classifies vocals/drums/bass/guitar/keys/synth/mix bus/master and re-weights the recipe, with 2 s hysteresis |

## Engineering checklist

These were requirements, not aspirations. Where each one lives:

- **Clean nonlinear processing (anti-aliasing)** — every static waveshaper uses first-order
  ADAA (antiderivative anti-aliasing), and the whole saturation suite runs inside the
  oversampled block. `src/dsp/SaturationSuite.h`
- **Dynamic behaviour, not just static curves** — dynamic harmonic generation, adaptive
  release in every compressor, programme-dependent limiter release, transient-aware limiting.
- **Tuned, musical parameter ranges** — macros are bounded to musical amounts (±3–4.5 dB EQ
  bands, ≤ 2.5:1 glue ratios); the recipe maps them on perceptual tapers. `src/dsp/Recipe.h`
- **Trustworthy metering** — K-weighted (BS.1770 pre-filter) loudness for in/out and
  auto-gain, peak/RMS, gain-reduction and correlation, all published lock-free.
  `src/dsp/Meters.h`
- **Solid under-the-hood engineering** — `ScopedNoDenormals` + explicit denormal flushing,
  lock-free atomics between audio and UI threads, preallocated everything (no allocation on
  the audio thread), SIMD-friendly JUCE vector ops, exact latency reporting.
- **CPU efficiency** — modules true-bypass when idle, oversampling is selectable, and the
  offline harness asserts faster-than-realtime processing at 4× oversampling.
- **Preset quality** — factory presets are curated starting points that teach the plugin
  (`src/state/Presets.h`), never max-settings demos.
- **Consistent gain staging** — input/output trims, per-stage level compensation in the
  saturators, and K-weighted auto-gain referenced to the chain input.
- **Stable, boring reliability** — the test harness (`tests/TestMain.cpp`) drives the real
  processor at 44.1/48/96/192 kHz, block sizes 1–4096, mono and stereo, fuzzes parameters,
  and checks nulls, latency honesty, denormal stalls and state round-trips on every CI run.
- **Selectable oversampling** — Auto / Off / 2× / 4× / 8× / 16× (Auto: 2× realtime, 8×
  offline render, relaxed at high sample rates). Runtime switches never allocate on the
  audio thread.
- **Auto gain-compensation** — K-weighted matched-loudness A/B, slew-limited, gated, ±18 dB.
  `src/dsp/AutoGain.h`
- **Delta / listen mode** — hear exactly what's added or removed, latency-aligned so it's
  phase-true.
- **Built-in dry/wet mix** — linear, latency-aligned crossfade for parallel processing.
- **Component variation / analog drift** — every instance is its own "unit off the line":
  a saved seed skews per-channel gains, corners, drive and timing by tolerance-realistic
  amounts, scaled by the Drift knob, re-rollable from the UI. `src/dsp/AnalogDrift.h`
- **Flexible sidechain** — external sidechain bus plus detector high-pass/low-pass filtering
  and adaptive-release envelope. `src/dsp/Detector.h`
- **Mid/side & stereo width handling** — M/S width, side sheen, bass mono, mono-safe ratio
  guard. `src/dsp/StereoProcessor.h`
- **A/B/C/D snapshots and undo history** — four full-state snapshots persisted with the
  session, plus a real undo manager on every parameter move. `src/state/Snapshots.h`
- **Accurate filter and phase design** — analog-matched (decramped) EQ curves that keep
  their shape near Nyquist at 44.1 kHz, graceful degeneracy handling up to 192 kHz, and a
  linear-phase EQ option with honest latency reporting. `src/dsp/MusicalEQ.h`
- **Frictionless licensing + format coverage** — VST3 / AU / AAX* / Standalone from one
  CMake project, macOS universal (Apple Silicon + Intel) by default, no dongle, no online
  activation, no iLok. Install it and it works.

\* AAX requires the Avid AAX SDK: point `SECRET_SAUCE_AAX_SDK_PATH` at it and the target
appears; signing for Pro Tools distribution is done with Avid/PACE tooling as usual.

## Building

Requirements: CMake ≥ 3.22, a C++20 compiler. JUCE 8.0.14 is fetched automatically (or point
`SECRET_SAUCE_JUCE_PATH` at a local checkout).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Linux needs the usual JUCE dev packages (`libasound2-dev libx11-dev libxrandr-dev
libxinerama-dev libxcursor-dev libfreetype-dev libfontconfig1-dev libcurl4-openssl-dev`).

Artefacts land in `build/SecretSauce_artefacts/Release/` (VST3, Standalone; AU on macOS;
AAX with the SDK).

### Tests

```bash
cmake --build build --target SecretSauceTests
ctest --test-dir build --output-on-failure
```

The harness is part of the definition of done: no release without a green run at every
supported sample rate and buffer size.

## Repository map

```
src/
  Parameters.h          the parameter contract (IDs, ranges, defaults)
  PluginProcessor.*     routing, oversampling, latency, mix/delta, state
  PluginEditor.*        UI: hero knob, macros, meters, snapshots, presets
  dsp/                  one header per processor + Recipe (the brain)
  state/                snapshots + factory presets
tests/TestMain.cpp      offline host: nulls, latency, fuzz, denormals, CPU
docs/DESIGN.md          deeper DSP + architecture notes
```

## License

GPL-3.0-or-later (this project builds against JUCE under its GPL option). No copy
protection of any kind — see LICENSE.
