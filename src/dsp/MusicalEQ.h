#pragma once

#include "DspHelpers.h"

namespace sauce::dsp
{
    // =========================================================================
    // MusicalEQ — six musical macros instead of a parametric EQ.
    //
    //   Weight   : 60 Hz low shelf (±4 dB) + subtle 40 Hz bell
    //   Warmth   : wide 200 Hz bell (±3 dB) + gentle downward tilt
    //   Body     : 400 Hz bell (±3 dB)
    //   Presence : 3.5 kHz bell (±3.5 dB)
    //   Sparkle  : 8 kHz bell + 16 kHz shelf blend
    //   Air      : 12 kHz high shelf (±4.5 dB)
    //
    // Requirements for implementers:
    //   - Minimum-phase path: analog-matched (decramped) coefficients — e.g.
    //     Vicanek matched biquads or Orfanidis peaking design — so high-shelf
    //     and HF bell curves keep their analog shape near Nyquist at 44.1 kHz
    //     instead of cramping. This is non-negotiable: Air/Sparkle live where
    //     bilinear-transform biquads are at their worst.
    //   - Linear-phase option: build a linear-phase FIR of the same composite
    //     magnitude response (FFT-sample the cascade magnitude, zero phase,
    //     window). Report group delay via latencySamples(). Rebuilds must not
    //     glitch or allocate on the audio thread mid-block (precompute /
    //     double-buffer; juce::dsp::Convolution's background loader is
    //     acceptable).
    //   - Character: vintage shifts centres down slightly and widens Q;
    //     modern is surgical.
    //   - Per-channel corner skew via Params::chanFreqMul (analog drift).
    //   - Coefficient updates smoothed/interpolated — sweeping any macro must
    //     be click-free and the filters unconditionally stable (44.1k..192k).
    //   - active==false must be a true bypass (no phase shift, no CPU beyond
    //     the check) once smoothing has settled.
    // =========================================================================
    class MusicalEQ
    {
    public:
        struct Params
        {
            float warmth    = 0.0f;   // all -1..1
            float air       = 0.0f;
            float body      = 0.0f;
            float presence  = 0.0f;
            float weight    = 0.0f;
            float sparkle   = 0.0f;
            float character = 1.0f;   // 0 = vintage .. 1 = modern
            bool  linearPhase = false;
            float chanFreqMul[2] { 1.0f, 1.0f };
            bool  active    = true;
        };

        void prepare (double newSampleRate, int maxBlockSize, int numChannels)
        {
            sampleRate = newSampleRate;
            juce::ignoreUnused (maxBlockSize, numChannels);
        }

        void reset() {}

        void update (const Params& p) { params = p; }

        void process (juce::AudioBuffer<float>& buffer, int numSamples)
        {
            juce::ignoreUnused (buffer, numSamples);
            // Passthrough scaffold — real implementation replaces this file.
        }

        /** Extra latency introduced by the linear-phase path (0 when min-phase). */
        int latencySamples() const { return 0; }

    private:
        double sampleRate = 48000.0;
        Params params;
    };
} // namespace sauce::dsp
