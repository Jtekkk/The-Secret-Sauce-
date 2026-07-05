#pragma once

#include "DspHelpers.h"

namespace sauce::dsp
{
    // =========================================================================
    // MicroDynamics — compression that doesn't sound like compression.
    //
    //   Glue    : slow feedback-style bus compressor, low ratio (1.4:1..2.5:1),
    //             auto makeup, blended in parallel (~50 %) so it reads as
    //             cohesion, not squash.
    //   Punch   : transient attack emphasis, low/mid weighted.
    //   Density : upward compression of low-level detail (max ~6 dB lift,
    //             gated so noise floors don't breathe up).
    //   Snap    : high-frequency transient emphasis.
    //
    // Requirements for implementers:
    //   - Detector input: use extEnv when non-null (per-sample envelope from
    //     Detector, linear domain), else derive internally.
    //   - Adaptive release everywhere (programme-dependent), never a fixed
    //     release that pumps.
    //   - Character: vintage = slower/softer knees, modern = faster/cleaner.
    //   - Per-channel timing skew via Params::chanTimeMul (analog drift);
    //     gain computation itself must be stereo-linked to protect the image.
    //   - Publish worst-case gain reduction via getGainReductionDb().
    //   - Click-free parameter changes; no allocation in process().
    // =========================================================================
    class MicroDynamics
    {
    public:
        struct Params
        {
            float glue      = 0.0f;   // 0..1
            float punch     = 0.0f;   // 0..1
            float density   = 0.0f;   // 0..1
            float snap      = 0.0f;   // 0..1
            float character = 1.0f;   // 0 = vintage .. 1 = modern
            float chanTimeMul[2] { 1.0f, 1.0f };
            bool  active    = true;
        };

        void prepare (double newSampleRate, int maxBlockSize, int numChannels)
        {
            sampleRate = newSampleRate;
            juce::ignoreUnused (maxBlockSize, numChannels);
        }

        void reset() {}

        void update (const Params& p) { params = p; }

        /** extEnv: optional per-sample detector envelope (numSamples long). */
        void process (juce::AudioBuffer<float>& buffer, int numSamples, const float* extEnv)
        {
            juce::ignoreUnused (buffer, numSamples, extEnv);
            // Passthrough scaffold — real implementation replaces this file.
        }

        float getGainReductionDb() const { return 0.0f; }

    private:
        double sampleRate = 48000.0;
        Params params;
    };
} // namespace sauce::dsp
