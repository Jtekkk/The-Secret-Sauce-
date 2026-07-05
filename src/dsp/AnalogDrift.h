#pragma once

#include "DspHelpers.h"

namespace sauce::dsp
{
    // =========================================================================
    // Component variation. Real hardware channels never match: resistor and
    // capacitor tolerances skew gains, corner frequencies and drive points a
    // little differently per channel and per unit. We model this with a seeded
    // RNG so each plugin instance gets its own stable "unit", and the two
    // channels of that unit differ slightly. The seed is saved with the
    // session so a mix recalls identically; the UI can re-roll it.
    //
    // 'depth' (0..1) scales all offsets; 0 = digitally perfect matching.
    // =========================================================================
    struct AnalogDrift
    {
        void setSeed (juce::int64 newSeed)
        {
            seed = newSeed;
            juce::Random rng (seed);

            for (int ch = 0; ch < 2; ++ch)
            {
                unit  [ch] = rng.nextFloat() * 2.0f - 1.0f;  // -1..1
                gain  [ch] = rng.nextFloat() * 2.0f - 1.0f;
                freq  [ch] = rng.nextFloat() * 2.0f - 1.0f;
                drive [ch] = rng.nextFloat() * 2.0f - 1.0f;
                timing[ch] = rng.nextFloat() * 2.0f - 1.0f;
            }
        }

        juce::int64 getSeed() const { return seed; }

        // All accessors return per-channel multipliers/offsets already scaled
        // by depth. Ranges are deliberately subtle — drift should be felt as
        // "alive", never heard as broken channel balance.

        /** ± 0.25 dB channel gain skew at full depth. */
        float gainTrimDb (int ch, float depth) const   { return gain[ch & 1] * 0.25f * depth; }

        /** ± 3 % filter corner skew at full depth. */
        float freqMul (int ch, float depth) const      { return 1.0f + freq[ch & 1] * 0.03f * depth; }

        /** ± 6 % saturation drive skew at full depth. */
        float driveMul (int ch, float depth) const     { return 1.0f + drive[ch & 1] * 0.06f * depth; }

        /** ± 4 % envelope timing skew at full depth. */
        float timeMul (int ch, float depth) const      { return 1.0f + timing[ch & 1] * 0.04f * depth; }

        /** Per-unit bias, -1..1 (same for both channels) — lets whole-unit
            character shift, like console channels from different production
            years. */
        float unitBias (float depth) const             { return unit[0] * depth; }

    private:
        juce::int64 seed = 0x5EC5A0CE;
        float unit[2] {}, gain[2] {}, freq[2] {}, drive[2] {}, timing[2] {};
    };
} // namespace sauce::dsp
