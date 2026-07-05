#pragma once

#include "DspHelpers.h"

namespace sauce::dsp
{
    // =========================================================================
    // Sidechain detector. Feeds MicroDynamics (and anything else that wants an
    // envelope) from either the internal signal or the external sidechain bus.
    //
    // - High-pass / low-pass detector filtering (TPT SVF, stable when swept)
    // - Peak/RMS hybrid envelope
    // - Adaptive release: fast recovery after brief peaks, slow on sustained
    //   material, which is most of why compression here doesn't "pump".
    // =========================================================================
    class Detector
    {
    public:
        struct Params
        {
            float highpassHz = 20.0f;
            float lowpassHz  = 20000.0f;
            bool  useExternal = false;   // informational; routing happens outside
        };

        void prepare (double sampleRate, int maxBlockSize, int /*numChannels*/)
        {
            sr = sampleRate;

            juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };
            hp.prepare (spec);
            lp.prepare (spec);
            hp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
            lp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

            fast.prepare (sampleRate, 0.1f, 40.0f);
            slow.prepare (sampleRate, 5.0f, 250.0f);

            envelope.resize ((size_t) maxBlockSize, 0.0f);
            reset();
        }

        void reset()
        {
            hp.reset();
            lp.reset();
            fast.reset();
            slow.reset();
            std::fill (envelope.begin(), envelope.end(), 0.0f);
        }

        void update (const Params& p)
        {
            hp.setCutoffFrequency (juce::jlimit (10.0f, (float) sr * 0.45f, p.highpassHz));
            lp.setCutoffFrequency (juce::jlimit (100.0f, (float) sr * 0.49f, p.lowpassHz));
        }

        /** Analyse 'source' (mono-summed) and fill the internal envelope buffer.
            Returns a pointer to numSamples envelope values (linear, >= 0). */
        const float* process (const juce::AudioBuffer<float>& source, int numSamples)
        {
            const int channels = juce::jmin (source.getNumChannels(), 2);
            const float norm = channels > 1 ? 0.5f : 1.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                float s = 0.0f;
                for (int ch = 0; ch < channels; ++ch)
                    s += source.getReadPointer (ch)[i];
                s *= norm;

                s = hp.processSample (0, s);
                s = lp.processSample (0, s);
                s = std::abs (s);

                // Peak/RMS hybrid with adaptive release: the slow follower
                // tracks programme level; the fast one rides peaks. Blend
                // favours the fast path when the signal is transient-rich.
                const float f = fast.processSample (s);
                const float sl = slow.processSample (s);
                envelope[(size_t) i] = flushDenormal (juce::jmax (sl, 0.6f * f + 0.4f * sl));
            }

            hp.snapToZero();
            lp.snapToZero();
            return envelope.data();
        }

        const float* lastEnvelope() const { return envelope.data(); }

    private:
        double sr = 48000.0;
        juce::dsp::StateVariableTPTFilter<float> hp, lp;
        BallisticsFilter fast, slow;
        std::vector<float> envelope;
    };
} // namespace sauce::dsp
