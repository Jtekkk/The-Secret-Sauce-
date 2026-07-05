#pragma once

#include "DspHelpers.h"

namespace sauce::dsp
{
    // =========================================================================
    // LowEndTightener — "Tight": bass that reads louder without being louder.
    //
    //   Dynamic low EQ : detector on the 40–150 Hz band drives a cut of the
    //                    resonant overhang only (max ~5 dB) — sustains and
    //                    booms duck, attacks pass.
    //   Bass harmonics : band-passed lows (≈40–120 Hz) through a soft
    //                    rectifying curve, mixed in quietly. Adds 2nd/3rd
    //                    harmonics the ear reads as "more bass" on small
    //                    speakers while the fundamental stays controlled.
    //   Phase align    : first-order allpass around 90 Hz nudges low-frequency
    //                    phase coherence (kick vs bass style smear).
    //   Rumble guard   : 18 Hz high-pass (12 dB/oct) fades in with tight > 0.
    //
    // Requirements for implementers:
    //   - All crossover/band filters stable and sensible 44.1k..192k.
    //   - Harmonic path must be level-compensated and low-passed after the
    //     nonlinearity (its output lives below ~300 Hz; aliasing is a
    //     non-issue there at base rate, but keep the curve gentle).
    //   - tight==0 must decay to bit-exact passthrough.
    //   - No allocation in process(); click-free parameter motion.
    // =========================================================================
    class LowEndTightener
    {
    public:
        struct Params
        {
            float tight     = 0.0f;   // 0..1 overall amount
            float bassHarm  = 0.0f;   // 0..1 harmonic bass level
            float character = 1.0f;   // 0 = vintage .. 1 = modern
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

        float getGainReductionDb() const { return 0.0f; }

    private:
        double sampleRate = 48000.0;
        Params params;
    };
} // namespace sauce::dsp
