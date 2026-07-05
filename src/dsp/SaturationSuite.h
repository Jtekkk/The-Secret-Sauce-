#pragma once

#include "DspHelpers.h"

namespace sauce::dsp
{
    // =========================================================================
    // SaturationSuite — the analog colour engine. Runs INSIDE the oversampled
    // block (prepare() receives the oversampled rate).
    //
    // Stages (in series, each blended by weight):
    //   1. Console model (Gold/Silver/Crimson/Emerald/Titanium/Copper):
    //      fixed harmonic recipe + gentle tone tilt per model.
    //   2. Tape: tanh-family curve with pre/de-emphasis (HF compression like
    //      real tape), ADAA anti-aliased.
    //   3. Tube: asymmetric curve (2nd harmonic forward), DC-blocked, ADAA.
    //   4. Transformer: low-frequency-weighted nonlinearity (LF thickens
    //      first, like core saturation), ADAA.
    //   5. Soft clip: cubic/tanh hybrid safety clip, ADAA.
    //   6. Dynamic harmonics: envelope-modulated drive so colour breathes with
    //      the programme instead of sitting statically on top.
    //
    // Requirements for implementers:
    //   - First-order ADAA (antiderivative anti-aliasing) on every static
    //     curve; the ill-conditioned small-difference case must fall back to
    //     the midpoint direct evaluation.
    //   - DC blockers after any asymmetric stage.
    //   - Approximate output level compensation per stage so drive changes
    //     colour, not loudness (final trim handled by AutoGain).
    //   - Per-channel drive skew from Params::chanDriveMul (analog drift).
    //   - All parameter changes click-free (internal one-pole smoothing).
    //   - No allocation, locks, or exceptions in process().
    // =========================================================================
    class SaturationSuite
    {
    public:
        struct Params
        {
            float driveDb        = 0.0f;   // 0..~18 dB into the stages
            float tape           = 0.0f;   // 0..1 stage weights
            float tube           = 0.0f;
            float transformer    = 0.0f;
            float clip           = 0.0f;
            float dynHarm        = 0.0f;   // 0..1 dynamic harmonic amount
            int   console        = 0;      // 0=off, 1..6 = model index
            float consoleAmt     = 0.0f;   // 0..1 console stage intensity
            float character      = 1.0f;   // 0 = vintage .. 1 = modern
            float chanDriveMul[2] { 1.0f, 1.0f }; // analog drift skew
            double oversampledRate = 96000.0;     // actual rate process() runs at
            bool  active         = true;
        };

        void prepare (const juce::dsp::ProcessSpec& spec)
        {
            sampleRate = spec.sampleRate;
            juce::ignoreUnused (spec);
        }

        void reset() {}

        void update (const Params& p) { params = p; }

        /** Process at the oversampled rate. */
        void process (juce::dsp::AudioBlock<float> block)
        {
            juce::ignoreUnused (block);
            // Passthrough scaffold — real implementation replaces this file.
        }

    private:
        double sampleRate = 96000.0;
        Params params;
    };
} // namespace sauce::dsp
