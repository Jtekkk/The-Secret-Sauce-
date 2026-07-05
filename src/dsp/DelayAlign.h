#pragma once

#include "DspHelpers.h"

namespace sauce::dsp
{
    // =========================================================================
    // Integer-sample delay used to keep the dry path (and the delta reference)
    // time-aligned with the processed path, so Mix and Delta stay phase-true
    // at any latency the wet chain reports.
    // =========================================================================
    class DelayAlign
    {
    public:
        void prepare (double /*sampleRate*/, int maxBlockSize, int numChannels, int maxDelaySamples)
        {
            maxDelay = juce::jmax (1, maxDelaySamples);
            buffer.setSize (numChannels, maxDelay + maxBlockSize + 8);
            reset();
        }

        void reset()
        {
            buffer.clear();
            writePos = 0;
        }

        void setDelay (int samples) { delay = juce::jlimit (0, maxDelay, samples); }
        int  getDelay() const       { return delay; }

        /** Push 'input' and fill 'output' with the delayed signal.
            Buffers may alias (in-place is fine). */
        void process (const juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output, int numSamples)
        {
            const int cap = buffer.getNumSamples();
            const int channels = juce::jmin (input.getNumChannels(), buffer.getNumChannels());

            for (int ch = 0; ch < channels; ++ch)
            {
                const float* in = input.getReadPointer (ch);
                float* ring     = buffer.getWritePointer (ch);
                float* out      = output.getWritePointer (ch);

                int w = writePos;
                for (int i = 0; i < numSamples; ++i)
                {
                    ring[w] = in[i];
                    int r = w - delay;
                    if (r < 0) r += cap;
                    out[i] = ring[r];
                    if (++w >= cap) w = 0;
                }
            }

            writePos = (writePos + numSamples) % cap;
        }

    private:
        juce::AudioBuffer<float> buffer;
        int writePos = 0, delay = 0, maxDelay = 1;
    };
} // namespace sauce::dsp
