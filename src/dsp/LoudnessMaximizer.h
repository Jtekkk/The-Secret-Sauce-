#pragma once

#include "DspHelpers.h"

namespace sauce::dsp
{
    // =========================================================================
    // LoudnessMaximizer — "Wow": loudness without the flatness.
    //
    //   Stage 1: soft clipper shaves the top 2–3 dB of hair so the limiter
    //            never works hard.
    //   Stage 2: lookahead peak limiter (~1.5 ms lookahead), smoothed gain
    //            envelope (attack ramp across the lookahead window, adaptive
    //            release), stereo-linked.
    //   Transient preservation: a transient detector briefly relaxes the
    //            release/depth on attacks so drums keep their front edge.
    //   Ceiling: limiter targets Params::ceilingDb with a small true-peak-ish
    //            guard margin (inter-sample estimate).
    //
    // CRITICAL latency contract:
    //   - latencySamples() must be CONSTANT for a given sample rate — the
    //     lookahead delay line stays in the path even at amount==0 (gain calc
    //     may be skipped, the delay may not). The processor reports this to
    //     the host once at prepare; it must never change with parameters.
    //
    // Requirements for implementers:
    //   - Gain envelope must be smooth (no zipper, no square-wave gain steps);
    //     use a windowed minimum + smoothing across the lookahead.
    //   - Adaptive release: fast after brief peaks, slow on dense material.
    //   - amount==0: unity gain apart from the constant delay (bit-exact
    //     delayed passthrough).
    //   - Publish worst-case gain reduction via getGainReductionDb().
    //   - No allocation in process().
    // =========================================================================
    class LoudnessMaximizer
    {
    public:
        struct Params
        {
            float amount            = 0.0f;   // 0..1 → up to ~+9 dB drive
            float ceilingDb         = -1.0f;  // -3..0
            float transientPreserve = 0.7f;   // 0..1
            float character         = 1.0f;   // 0 = vintage .. 1 = modern
            bool  active            = true;
        };

        void prepare (double newSampleRate, int maxBlockSize, int numChannels)
        {
            sampleRate = newSampleRate;
            lookahead = juce::jmax (16, (int) std::ceil (0.0015 * sampleRate));

            delay.setSize (juce::jmax (1, numChannels), lookahead + maxBlockSize + 8);
            delay.clear();
            writePos = 0;
        }

        void reset()
        {
            delay.clear();
            writePos = 0;
        }

        void update (const Params& p) { params = p; }

        void process (juce::AudioBuffer<float>& buffer, int numSamples)
        {
            // Scaffold: constant-latency delayed passthrough (the latency
            // contract holds from day one). Real implementation adds the
            // clipper + limiter around this delay.
            const int cap = delay.getNumSamples();
            const int channels = juce::jmin (buffer.getNumChannels(), delay.getNumChannels());

            for (int ch = 0; ch < channels; ++ch)
            {
                float* data = buffer.getWritePointer (ch);
                float* ring = delay.getWritePointer (ch);

                int w = writePos;
                for (int i = 0; i < numSamples; ++i)
                {
                    ring[w] = data[i];
                    int r = w - lookahead;
                    if (r < 0) r += cap;
                    data[i] = ring[r];
                    if (++w >= cap) w = 0;
                }
            }

            writePos = (writePos + numSamples) % cap;
        }

        /** Constant for a given prepare(); see latency contract above. */
        int latencySamples() const { return lookahead; }

        float getGainReductionDb() const { return 0.0f; }

    private:
        double sampleRate = 48000.0;
        int lookahead = 72;
        juce::AudioBuffer<float> delay;
        int writePos = 0;
        Params params;
    };
} // namespace sauce::dsp
