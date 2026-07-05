#pragma once

#include "DspHelpers.h"

namespace sauce::dsp
{
    // =========================================================================
    // StereoProcessor — width with a seatbelt.
    //
    //   Width       : M/S ratio scaling, 0..200 %. Above 100 % the side gain
    //                 rises on an energy-compensated curve (mid drops slightly)
    //                 so it reads as "wider", not "louder".
    //   Side sheen  : gentle side-channel HF shelf tied to the recipe, adds
    //                 space without pushing the mid out of focus.
    //   Bass mono   : side high-pass below bassMonoHz (2nd order) — low end
    //                 stays anchored and vinyl/club safe.
    //   Mono safe   : when enabled, side energy is dynamically ceilinged
    //                 relative to mid so the mono fold-down can never collapse
    //                 (correlation guard). This is the "protection" part and
    //                 must act transparently — a slow limiter on S/M ratio,
    //                 not a hard clamp.
    //
    // Requirements for implementers:
    //   - Mono input (1 channel): process() must be a no-op.
    //   - No Haas delays while monoSafe is on; width beyond 160 % with
    //     monoSafe off may add a sub-1 ms micro-delay on the side only.
    //   - Width changes click-free; all filters stable 44.1k..192k.
    //   - active==false (or width==100 %, no sheen, no bass-mono) must decay
    //     to a bit-exact passthrough.
    // =========================================================================
    class StereoProcessor
    {
    public:
        struct Params
        {
            float width      = 1.0f;   // 0..2 (1 = unchanged)
            float sideSheen  = 0.0f;   // 0..1 side HF lift
            bool  monoSafe   = true;
            float bassMonoHz = 120.0f; // 0 = off
            float character  = 1.0f;
            bool  active     = true;
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

    private:
        double sampleRate = 48000.0;
        Params params;
    };
} // namespace sauce::dsp
