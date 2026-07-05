#pragma once

#include "DspHelpers.h"
#include "Meters.h"

namespace sauce::dsp
{
    // =========================================================================
    // Auto gain compensation for honest, matched-loudness A/B.
    //
    // Measures K-weighted loudness of the signal entering the chain and of the
    // processed signal, then applies the difference (slewed, clamped) to the
    // wet path. Loudness bias is the oldest trick in the "my plugin sounds
    // better" book — this removes it so Mix/Bypass comparisons tell the truth.
    //
    // - Gated below -70 LUFS so silence doesn't drag the estimate around
    // - Slew limited (dB/s) so correction is never audible as pumping
    // - Clamped to ±18 dB as a sanity bound
    // =========================================================================
    class AutoGain
    {
    public:
        void prepare (double sampleRate, int maxBlockSize, int numChannels)
        {
            sr = sampleRate;
            preLoud.prepare (sampleRate, numChannels, 1.0);
            postLoud.prepare (sampleRate, numChannels, 1.0);
            gainSmooth.reset (sampleRate, 0.05);
            gainSmooth.setCurrentAndTargetValue (1.0f);
            currentDb = 0.0f;
            juce::ignoreUnused (maxBlockSize);
        }

        void reset()
        {
            preLoud.reset();
            postLoud.reset();
            gainSmooth.setCurrentAndTargetValue (1.0f);
            currentDb = 0.0f;
        }

        /** Call with the signal entering the wet chain (post input trim). */
        void measurePre (const juce::AudioBuffer<float>& buffer, int numSamples)
        {
            preDb = preLoud.processBlock (buffer, numSamples);
        }

        /** Call with the processed signal (pre mix). */
        void measurePost (const juce::AudioBuffer<float>& buffer, int numSamples)
        {
            postDb = postLoud.processBlock (buffer, numSamples);
        }

        /** Update the target compensation and apply it to 'buffer' if enabled. */
        void apply (juce::AudioBuffer<float>& buffer, int numSamples, bool enabled)
        {
            const bool gated = preDb < gateLufs || postDb < gateLufs;

            if (enabled && ! gated)
            {
                float target = juce::jlimit (-maxDb, maxDb, preDb - postDb);

                // Slew limit in dB domain: max 6 dB/s of movement.
                const float maxStep = 6.0f * (float) (numSamples / sr);
                currentDb = juce::jlimit (currentDb - maxStep, currentDb + maxStep, target);
            }
            else if (! enabled)
            {
                const float maxStep = 24.0f * (float) (numSamples / sr);
                currentDb = juce::jlimit (currentDb - maxStep, currentDb + maxStep, 0.0f);
            }
            // enabled && gated: hold the last correction.

            gainSmooth.setTargetValue (dbToGain (currentDb));

            if (! gainSmooth.isSmoothing() && juce::approximatelyEqual (gainSmooth.getTargetValue(), 1.0f))
                return;

            for (int i = 0; i < numSamples; ++i)
            {
                const float g = gainSmooth.getNextValue();
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.getWritePointer (ch)[i] *= g;
            }
        }

        float appliedDb() const { return currentDb; }
        float inputLufs() const  { return preDb; }
        float outputLufs() const { return postDb; }

    private:
        static constexpr float gateLufs = -70.0f;
        static constexpr float maxDb    = 18.0f;

        double sr = 48000.0;
        LoudnessEstimator preLoud, postLoud;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmooth;
        float preDb = silenceFloorDb, postDb = silenceFloorDb, currentDb = 0.0f;
    };
} // namespace sauce::dsp
