# The Secret Sauce — design notes

This document explains the architecture and the reasoning behind it. The README says *what*;
this says *why*.

## 1. Signal flow

```
input ──► input trim ──► [taps: meters, auto-gain ref, program sensor, detector]
      ──► LowEndTightener        (base rate)
      ──► MusicalEQ              (base rate; min-phase or linear-phase FIR)
      ──► MicroDynamics          (base rate; detector env, ext sidechain capable)
      ──► ▲ oversample ▲
            SaturationSuite      (console ► tape ► tube ► transformer ► clip, ADAA)
          ▼ decimate ▼
      ──► StereoProcessor        (M/S width, sheen, bass mono, mono-safe guard)
      ──► LoudnessMaximizer      (soft clip + lookahead limiter, constant latency)
      ──► AutoGain               (K-weighted match to chain input)
      ──► output trim
      ──► MIX  (dry path delayed by exactly the chain latency)
      ──► DELTA monitor (out − aligned dry)
```

Ordering rationale:

- **Tight before EQ**: the dynamic low control should react to the source's real low end,
  not to whatever Weight/Warmth just added.
- **EQ before dynamics**: macros shape what the compressors hear (classic "EQ into glue").
- **Saturation after dynamics, oversampled**: harmonics generated *after* level control sit
  more stably; the nonlinear stages are the only aliasing producers, so they alone pay the
  oversampling cost.
- **Width before limiter**: the limiter must see (and control) the widened signal, otherwise
  width changes the ceiling behaviour.
- **AutoGain before Mix**: parallel blends stay honest — the wet path is loudness-matched to
  the input before it is mixed against the dry path.

## 2. The Recipe (`src/dsp/Recipe.h`)

One function turns user macros + sensed programme + drift into every module's settings.
Two invariants:

1. **Sauce is a baseline, knobs are additive.** Sauce raises many processors a little
   (smoothstep taper, programme-scaled); the named knobs add on top and always work even at
   Sauce = 0.
2. **All-zero in, bit-exact out.** With everything at zero every module reports
   `active = false` and true-bypasses. The test harness nulls the whole chain against the
   input; this is enforced, not aspirational.

Programme scaling is a small table (vocals/drums/bass/…): e.g. Bass gets no width and extra
tight; Master gets gentler saturation and more limiter share. The "AI detection" is a
transparent feature heuristic (crest factor, band balance, onset rate, stereo ratio) with
2-second hysteresis — deliberately explainable, deliberately biased toward the safe
mix-bus recipe when unsure.

## 3. Latency accounting

Three latency sources: oversampler FIR (integer-latency mode), limiter lookahead
(constant 1.5 ms per prepare), linear-phase EQ ((taps−1)/2 when engaged).

- The processor sums them, calls `setLatencySamples`, and notifies the host from the message
  thread (`AsyncUpdater`) when a quality switch changes it.
- The dry path runs through `DelayAlign` set to the *same* total, so Mix and Delta stay
  phase-true at any setting.
- The limiter's delay stays in the path at amount = 0 — latency never depends on a
  continuous parameter, only on the explicit quality switches (oversampling, linear phase).

## 4. Anti-aliasing strategy

Static curves use first-order ADAA: `y = (F(x₁) − F(x₀)) / (x₁ − x₀)` with the exact
antiderivative and a midpoint fallback when the difference is ill-conditioned. On top of
that the whole suite runs 2–16× oversampled (FIR equiripple half-band, integer latency).
ADAA plus modest oversampling beats brute-force oversampling alone at equal CPU: ADAA kills
the high-order fold-back that even 8× can't fully suppress, and 2× removes ADAA's slight
HF droop from the audible band.

`Auto` oversampling: 2× realtime, 8× offline render (relaxed at ≥ 88.2 kHz where the budget
is already spent). All five oversamplers are constructed in `prepareToPlay`, so runtime
switching allocates nothing on the audio thread.

## 5. Filters near Nyquist

Air lives at 12 kHz and Sparkle touches 16 kHz — exactly where bilinear-transform biquads
cramp at 44.1 kHz. The EQ uses analog-matched designs (Vicanek matched biquads / Orfanidis
peaking) so the curve at 44.1 kHz matches the curve at 96 kHz. Band frequencies are clamped
below 0.45·fs, and degenerate bands collapse to shelves/identity rather than going unstable.
The linear-phase option rebuilds the same composite magnitude as a zero-phase FIR sampled
from the *analog* prototype (no cramping by construction) and reports its group delay.

## 6. Threading model

- **Audio thread**: reads parameter atomics, computes the recipe, processes. No locks, no
  allocation, no exceptions. `ScopedNoDenormals` plus explicit flushing in feedback paths.
- **Message thread**: editor, undo, snapshots, presets, latency notification, drift re-roll.
- **Exchange**: `MeterValues` (relaxed atomics) audio→UI; parameter atomics UI→audio;
  JUCE's Convolution background loader for linear-phase IR swaps.

## 7. Analog drift (`src/dsp/AnalogDrift.h`)

A seeded RNG derives per-channel multipliers within component-tolerance ranges
(±0.25 dB gain, ±3 % corners, ±6 % drive, ±4 % timing at full depth). The seed is generated
per instance, saved in the session, and re-rollable from the UI ("new unit off the line").
Gain *computation* in dynamics stays stereo-linked — drift skews timing and tone, never the
image. The Drift knob scales all offsets; 0 = digitally perfect matching.

## 8. Honest loudness

- Auto-gain and the meters share one loudness scale: K-weighted (BS.1770 pre-filters),
  400 ms integration for the match, gated below −70 LUFS, slew-limited (6 dB/s), ±18 dB.
- The limiter aims at `ceiling − 0.3 dB` with a parabolic inter-sample estimate. It is
  documented as an estimate — not a certified ITU true-peak meter.
- Delta output is `processed − aligned dry`: with auto-gain on, what you hear in Delta is
  genuinely the added colour, not a level difference.

## 9. Testing philosophy (`tests/TestMain.cpp`)

The harness is a miniature host, run by CI on every push:

| Test | Contract enforced |
|---|---|
| Lifecycle sweep | 44.1–192 kHz × block 16–4096 never produces non-finite output |
| Impulse latency | the impulse lands at exactly `getLatencySamples()` |
| Neutral null | all-zero settings null against the input (< 1e-3 RMS) |
| Mix = 0 | heavy settings + mix 0 still returns the aligned dry signal |
| Delta neutral | delta of a neutral chain is silence |
| State round trip | params + drift seed + snapshots survive save/load |
| Fuzz | 40 rounds of random parameters stay finite |
| Denormals | 1e-30 inputs and post-signal silence don't stall or explode |
| Block invariance | 512-sample and 64-sample runs of the same audio agree |
| CPU smoke | faster than realtime at 4× oversampling on a modest VM |

## 10. Formats and licensing

One CMake project produces VST3, AU (macOS), AAX (with the Avid SDK path set) and a
standalone app; macOS builds are universal (arm64 + x86_64) by default. There is no copy
protection, no dongle, no online activation anywhere in the code — "frictionless licensing"
is implemented by *not writing* a licensing system. The project itself is GPL-3.0-or-later
(JUCE GPL option).
