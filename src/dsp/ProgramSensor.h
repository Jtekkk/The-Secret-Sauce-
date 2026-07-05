#pragma once

#include "DspHelpers.h"
#include "../Parameters.h"

namespace sauce::dsp
{
    // =========================================================================
    // ProgramSensor — source detection ("AI Mix Detection", honestly labelled:
    // a transparent feature heuristic, not a network).
    //
    // Accumulates cheap block features from the *input* signal:
    //   - crest factor (peak/RMS)
    //   - spectral balance: low (<150 Hz) / mid / high (>5 kHz) energy ratios
    //   - transient rate (onsets per second)
    //   - stereo correlation & side energy
    //   - pitch-band concentration (is energy narrowband like bass/vocal or
    //     wideband like a mix?)
    //
    // Classifies into sauce::param::Program (vocals/drums/bass/guitar/keys/
    // synth/mixBus/master) with:
    //   - a rolling feature window of a few seconds
    //   - hysteresis: only re-classify after sustained evidence (≥ 2 s),
    //     so the recipe never flip-flops mid-phrase
    //   - confidence 0..1; below ~0.4 fall back to mixBus (the safe default)
    //
    // Requirements for implementers:
    //   - analyze() must be cheap (a few filters + accumulators) — it runs on
    //     every block at base rate.
    //   - No allocation/locks in analyze(); classification math may run at
    //     block rate.
    //   - current() returns a *stable* value between hysteresis flips.
    // =========================================================================
    class ProgramSensor
    {
    public:
        void prepare (double newSampleRate, int maxBlockSize, int numChannels)
        {
            sampleRate = newSampleRate;
            juce::ignoreUnused (maxBlockSize, numChannels);
            resetState();
        }

        void reset() { resetState(); }

        void analyze (const juce::AudioBuffer<float>& input, int numSamples)
        {
            juce::ignoreUnused (input, numSamples);
            // Passthrough scaffold — real implementation replaces this file.
        }

        /** Stable detected program (hysteresis applied). */
        sauce::param::Program current() const { return detected; }

        /** 0..1 confidence in the current classification. */
        float confidence() const { return conf; }

    private:
        void resetState()
        {
            detected = sauce::param::Program::mixBus;
            conf = 0.0f;
        }

        double sampleRate = 48000.0;
        sauce::param::Program detected = sauce::param::Program::mixBus;
        float conf = 0.0f;
    };
} // namespace sauce::dsp
