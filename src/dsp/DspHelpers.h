#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>

// =============================================================================
// Shared DSP utilities: conversions, smoothing, safe math.
// Everything here is allocation-free and real-time safe unless noted.
// =============================================================================

namespace sauce::dsp
{
    inline constexpr float silenceFloorDb = -120.0f;

    inline float dbToGain (float db)   { return juce::Decibels::decibelsToGain (db, silenceFloorDb); }
    inline float gainToDb (float gain) { return juce::Decibels::gainToDecibels (gain, silenceFloorDb); }

    /** 0..100 knob -> 0..1 */
    inline float pct (float v) { return v * 0.01f; }

    /** -100..100 knob -> -1..1 */
    inline float bipolar (float v) { return v * 0.01f; }

    /** Perceptual taper for macro knobs: gentle at the start, assertive at the top. */
    inline float macroTaper (float x01) { return x01 * x01 * (3.0f - 2.0f * x01); } // smoothstep

    inline float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }

    /** Kill NaN/inf that could otherwise poison feedback paths. */
    inline float sanitize (float x) { return std::isfinite (x) ? x : 0.0f; }

    /** Flush tiny values that would denormalise recursive filters. */
    inline float flushDenormal (float x) { return std::abs (x) < 1.0e-25f ? 0.0f : x; }

    /** Fast, bounded tanh approximation (Pade 3/2), max error ~1e-4 in [-3,3]. */
    inline float fastTanh (float x)
    {
        x = juce::jlimit (-5.0f, 5.0f, x);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    /** One-pole smoother with independent attack/release, coefficient form. */
    struct BallisticsFilter
    {
        void prepare (double sampleRate, float attackMs, float releaseMs)
        {
            sr = sampleRate;
            setTimes (attackMs, releaseMs);
            state = 0.0f;
        }

        void setTimes (float attackMs, float releaseMs)
        {
            aCoef = coefForMs (attackMs);
            rCoef = coefForMs (releaseMs);
        }

        float processSample (float x)
        {
            const float coef = x > state ? aCoef : rCoef;
            state = flushDenormal (x + coef * (state - x));
            return state;
        }

        void reset (float v = 0.0f) { state = v; }

        float coefForMs (float ms) const
        {
            if (ms <= 0.0f) return 0.0f;
            return (float) std::exp (-1.0 / (0.001 * ms * sr));
        }

        double sr = 48000.0;
        float aCoef = 0.0f, rCoef = 0.0f, state = 0.0f;
    };

    /** Block-rate linear parameter smoother (per-sample ramp inside a block). */
    struct SmoothedGain
    {
        void prepare (double sampleRate, float rampSeconds = 0.02f)
        {
            value.reset (sampleRate, rampSeconds);
        }

        void setTargetDb (float db)     { value.setTargetValue (dbToGain (db)); }
        void setTargetLinear (float g)  { value.setTargetValue (g); }
        void snap (float g)             { value.setCurrentAndTargetValue (g); }
        bool isSmoothing() const        { return value.isSmoothing(); }

        void applyTo (juce::AudioBuffer<float>& buffer, int numSamples)
        {
            if (! value.isSmoothing())
            {
                const float g = value.getTargetValue();
                if (g != 1.0f)
                    buffer.applyGain (0, numSamples, g);
                return;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                const float g = value.getNextValue();
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.getWritePointer (ch)[i] *= g;
            }
        }

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> value;
    };

    /** True-peak-ish estimate: parabolic interpolation between samples. Cheap,
        good enough for metering guards (not a certified ITU true-peak meter). */
    inline float estimatePeak (const float* data, int n)
    {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = juce::jmax (peak, std::abs (data[i]));
        return peak;
    }
} // namespace sauce::dsp
