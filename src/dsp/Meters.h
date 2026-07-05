#pragma once

#include "DspHelpers.h"
#include <atomic>

namespace sauce::dsp
{
    // =========================================================================
    // Lock-free meter bus. The audio thread publishes; the editor polls on a
    // Timer. All exchange happens through relaxed atomics — no locks, no
    // allocation, no waiting on either side.
    //
    // Loudness values are K-weighted (BS.1770 pre-filter pair) short-term-ish
    // measurements so Auto Gain, the meters and the A/B loudness match all
    // reference the same scale.
    // =========================================================================
    struct MeterValues
    {
        std::atomic<float> inPeak[2]   { { 0.0f }, { 0.0f } };
        std::atomic<float> outPeak[2]  { { 0.0f }, { 0.0f } };
        std::atomic<float> inRms[2]    { { 0.0f }, { 0.0f } };
        std::atomic<float> outRms[2]   { { 0.0f }, { 0.0f } };
        std::atomic<float> inLufs      { silenceFloorDb };
        std::atomic<float> outLufs     { silenceFloorDb };
        std::atomic<float> grDynamicsDb{ 0.0f };   // micro-dynamics gain reduction (positive dB)
        std::atomic<float> grLimiterDb { 0.0f };   // maximizer gain reduction (positive dB)
        std::atomic<float> correlation { 1.0f };   // -1..1 stereo correlation
        std::atomic<float> autoGainDb  { 0.0f };   // currently applied compensation
        std::atomic<int>   detectedProgram { 0 };  // sauce::param::Program (sensed)
        std::atomic<bool>  clipped     { false };
    };

    /** K-weighting pre-filter (shelf + high-pass) per BS.1770-4, coefficients
        computed for the running sample rate. */
    class KWeighting
    {
    public:
        void prepare (double sampleRate, int numChannels)
        {
            using Coeffs = juce::dsp::IIR::Coefficients<float>;

            // BS.1770-4 pre-filter pair computed with the De Man closed form,
            // which reproduces the ITU reference tables exactly at any sample
            // rate. (RBJ makeHighShelf/makeHighPass deviate by up to 0.5 dB
            // around 1.6 kHz — enough to skew LUFS on midrange-heavy material.)
            const double pi = juce::MathConstants<double>::pi;

            // Stage 1: "head" high shelf.
            const double f0 = 1681.9744509555319;
            const double G  = 3.99984385397;
            const double Qs = 0.7071752369554193;
            const double K  = std::tan (pi * f0 / sampleRate);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Qs + K * K;

            Coeffs::Ptr shelf = new Coeffs ((float) ((Vh + Vb * K / Qs + K * K) / a0),
                                            (float) (2.0 * (K * K - Vh) / a0),
                                            (float) ((Vh - Vb * K / Qs + K * K) / a0),
                                            1.0f,
                                            (float) (2.0 * (K * K - 1.0) / a0),
                                            (float) ((1.0 - K / Qs + K * K) / a0));

            // Stage 2: high-pass with the ITU's unnormalised numerator [1 -2 1].
            const double fh = 38.13547087602444;
            const double Qh = 0.5003270373238773;
            const double Kh = std::tan (pi * fh / sampleRate);
            const double ah = 1.0 + Kh / Qh + Kh * Kh;

            Coeffs::Ptr hp = new Coeffs (1.0f, -2.0f, 1.0f,
                                         1.0f,
                                         (float) (2.0 * (Kh * Kh - 1.0) / ah),
                                         (float) ((1.0 - Kh / Qh + Kh * Kh) / ah));

            for (int ch = 0; ch < juce::jmin (numChannels, 2); ++ch)
            {
                shelfF[ch].coefficients = shelf;
                hpF[ch].coefficients    = hp;
                shelfF[ch].reset();
                hpF[ch].reset();
            }
        }

        void reset()
        {
            for (auto& f : shelfF) f.reset();
            for (auto& f : hpF)    f.reset();
        }

        /** Returns the mean-square of the K-weighted block (summed channels,
            per BS.1770 channel weighting = 1.0 for L/R). Input is not modified. */
        double processBlockMeanSquare (const juce::AudioBuffer<float>& buffer, int numSamples)
        {
            double sum = 0.0;
            const int channels = juce::jmin (buffer.getNumChannels(), 2);

            for (int ch = 0; ch < channels; ++ch)
            {
                const float* data = buffer.getReadPointer (ch);
                double chSum = 0.0;

                for (int i = 0; i < numSamples; ++i)
                {
                    float s = shelfF[ch].processSample (data[i]);
                    s = hpF[ch].processSample (s);
                    chSum += (double) s * s;
                }

                shelfF[ch].snapToZero();
                hpF[ch].snapToZero();
                sum += chSum;
            }

            return numSamples > 0 ? sum / numSamples : 0.0;
        }

    private:
        juce::dsp::IIR::Filter<float> shelfF[2], hpF[2];
    };

    /** Sliding short-term loudness (3 s window approximated with a one-pole
        integrator — smooth, cheap, and stable across block sizes). */
    class LoudnessEstimator
    {
    public:
        void prepare (double sampleRate, int numChannels, double windowSeconds = 0.4)
        {
            kw.prepare (sampleRate, numChannels);
            window = windowSeconds;
            sr = sampleRate;
            ms = 0.0;
        }

        void reset() { kw.reset(); ms = 0.0; }

        /** Feed a block; returns current loudness in LUFS-like dB. */
        float processBlock (const juce::AudioBuffer<float>& buffer, int numSamples)
        {
            const double blockMs = kw.processBlockMeanSquare (buffer, numSamples);
            const double dt = numSamples / sr;
            const double alpha = 1.0 - std::exp (-dt / window);
            ms += alpha * (blockMs - ms);

            if (ms < 1.0e-12)
                return silenceFloorDb;

            return float (-0.691 + 10.0 * std::log10 (ms));
        }

        float current() const
        {
            return ms < 1.0e-12 ? silenceFloorDb
                                : float (-0.691 + 10.0 * std::log10 (ms));
        }

    private:
        KWeighting kw;
        double ms = 0.0, window = 0.4, sr = 48000.0;
    };

    /** Publishes peak / RMS / correlation for a buffer into MeterValues. */
    inline void publishLevels (const juce::AudioBuffer<float>& buffer, int numSamples,
                               std::atomic<float>* peakSlots, std::atomic<float>* rmsSlots,
                               std::atomic<float>* correlation = nullptr)
    {
        const int channels = juce::jmin (buffer.getNumChannels(), 2);

        for (int ch = 0; ch < channels; ++ch)
        {
            const float peak = buffer.getMagnitude (ch, 0, numSamples);
            const float rms  = buffer.getRMSLevel (ch, 0, numSamples);

            // Publish max-hold style: editor decays, audio thread only raises
            // or refreshes.
            peakSlots[ch].store (peak, std::memory_order_relaxed);
            rmsSlots[ch].store (rms, std::memory_order_relaxed);
        }

        if (channels == 1)
        {
            peakSlots[1].store (peakSlots[0].load (std::memory_order_relaxed), std::memory_order_relaxed);
            rmsSlots[1].store (rmsSlots[0].load (std::memory_order_relaxed), std::memory_order_relaxed);
        }

        if (correlation != nullptr && channels == 2 && numSamples > 0)
        {
            const float* l = buffer.getReadPointer (0);
            const float* r = buffer.getReadPointer (1);
            double lr = 0.0, ll = 0.0, rr = 0.0;

            for (int i = 0; i < numSamples; ++i)
            {
                lr += (double) l[i] * r[i];
                ll += (double) l[i] * l[i];
                rr += (double) r[i] * r[i];
            }

            const double denom = std::sqrt (ll * rr);
            correlation->store (denom > 1.0e-12 ? (float) (lr / denom) : 1.0f,
                                std::memory_order_relaxed);
        }
    }
} // namespace sauce::dsp
