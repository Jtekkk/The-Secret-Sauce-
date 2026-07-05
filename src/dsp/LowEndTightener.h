#pragma once

#include "DspHelpers.h"
#include <atomic>

namespace sauce::dsp
{
    // =========================================================================
    // LowEndTightener — "Tight": bass that reads louder without being louder.
    //
    //   Dynamic low EQ : detector on the 40–150 Hz band drives a cut of the
    //                    resonant overhang only (max ~5 dB) — sustains and
    //                    booms duck, attacks pass.
    //   Bass harmonics : band-passed lows (≈40–120 Hz) through a soft
    //                    rectifying curve, mixed in quietly. Adds 2nd/3rd
    //                    harmonics the ear reads as "more bass" on small
    //                    speakers while the fundamental stays controlled.
    //   Phase align    : first-order allpass around 90 Hz nudges low-frequency
    //                    phase coherence (kick vs bass style smear).
    //   Rumble guard   : 18 Hz high-pass (12 dB/oct) fades in with tight > 0.
    //
    // Requirements for implementers:
    //   - All crossover/band filters stable and sensible 44.1k..192k.
    //   - Harmonic path must be level-compensated and low-passed after the
    //     nonlinearity (its output lives below ~300 Hz; aliasing is a
    //     non-issue there at base rate, but keep the curve gentle).
    //   - tight==0 must decay to bit-exact passthrough.
    //   - No allocation in process(); click-free parameter motion.
    //
    // Topology: 2nd-order lowpass split at ~150 Hz, highs derived by
    // subtraction so recombination is exact by construction. All processing
    // (allpass blend, rumble guard, downward gain, harmonics) touches the low
    // band only; the dynamic gain is stereo-linked with an auto threshold that
    // rides a slow programme reference. Once the amount smoothers settle at
    // zero the block is returned untouched (bit-exact null).
    // =========================================================================
    class LowEndTightener
    {
    public:
        struct Params
        {
            float tight     = 0.0f;   // 0..1 overall amount
            float bassHarm  = 0.0f;   // 0..1 harmonic bass level
            float character = 1.0f;   // 0 = vintage .. 1 = modern
            float chanFreqMul[2] { 1.0f, 1.0f };
            bool  active    = true;
        };

        void prepare (double newSampleRate, int maxBlockSize, int numChannels)
        {
            sampleRate = newSampleRate;
            juce::ignoreUnused (maxBlockSize, numChannels);

            envFollower.prepare (sampleRate, attackMs, 250.0f);
            programmeRef.prepare (sampleRate, 400.0f, 900.0f);

            tightSmooth.reset (sampleRate, 0.03);
            harmSmooth.reset (sampleRate, 0.03);
            charSmooth.reset (sampleRate, 0.05);
            tightSmooth.setCurrentAndTargetValue (0.0f);
            harmSmooth.setCurrentAndTargetValue (0.0f);
            charSmooth.setCurrentAndTargetValue (params.character);

            grDezipCoef = (float) std::exp (-1.0 / (0.003 * sampleRate));

            reset();
        }

        void reset()
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                splitLp[ch].reset();
                phaseAp[ch].reset();
                rumbleHp[ch].reset();
                harmHp[ch].reset();
                harmLp[ch].reset();
                harmPostLp[ch].reset();
                dcBlock[ch].reset();
                dcBlock[ch].setCutoff (sampleRate, 10.0f);
            }

            envFollower.reset();
            programmeRef.reset();
            grDbState = 0.0f;
            settled   = false;

            tightSmooth.setCurrentAndTargetValue (tightSmooth.getTargetValue());
            harmSmooth.setCurrentAndTargetValue (harmSmooth.getTargetValue());
            charSmooth.setCurrentAndTargetValue (charSmooth.getTargetValue());

            grMeterDb.store (0.0f, std::memory_order_relaxed);
        }

        void update (const Params& p)
        {
            params.tight     = clamp01 (sanitize (p.tight));
            params.bassHarm  = clamp01 (sanitize (p.bassHarm));
            params.character = clamp01 (sanitize (p.character));
            params.active    = p.active;

            for (int ch = 0; ch < 2; ++ch)
            {
                const float m = sanitize (p.chanFreqMul[ch]);
                params.chanFreqMul[ch] = juce::jlimit (0.25f, 4.0f, m > 0.0f ? m : 1.0f);
            }
        }

        void process (juce::AudioBuffer<float>& buffer, int numSamples)
        {
            const int channels = juce::jmin (buffer.getNumChannels(), 2);
            if (numSamples <= 0 || channels <= 0)
                return;

            const float tightTarget = params.active ? params.tight    : 0.0f;
            const float harmTarget  = params.active ? params.bassHarm : 0.0f;
            tightSmooth.setTargetValue (tightTarget);
            harmSmooth.setTargetValue (harmTarget);
            charSmooth.setTargetValue (params.character);

            // ---- Settled null: hand the block back untouched (bit-exact) ----
            if (tightTarget <= settleEps && harmTarget <= settleEps
                && ! tightSmooth.isSmoothing() && tightSmooth.getCurrentValue() <= settleEps
                && ! harmSmooth.isSmoothing()  && harmSmooth.getCurrentValue()  <= settleEps
                && grDbState <= 1.0e-4f)
            {
                if (! settled)
                {
                    reset();        // fresh state for the next engage
                    settled = true;
                }
                grMeterDb.store (0.0f, std::memory_order_relaxed);
                return;
            }
            settled = false;

            // ---- Block-rate coefficient refresh (drift-skewed corners) ------
            for (int ch = 0; ch < channels; ++ch)
            {
                const float mul = params.chanFreqMul[ch];
                splitLp[ch].setCutoff    (sampleRate, splitHz    * mul, butterQ);
                phaseAp[ch].setCutoff    (sampleRate, phaseHz    * mul);
                rumbleHp[ch].setCutoff   (sampleRate, rumbleHz   * mul, butterQ);
                harmHp[ch].setCutoff     (sampleRate, harmLoHz   * mul, butterQ);
                harmLp[ch].setCutoff     (sampleRate, harmHiHz   * mul, butterQ);
                harmPostLp[ch].setCutoff (sampleRate, harmPostHz * mul, butterQ);
            }

            // ---- Adaptive release: quick after hits, patient on sustains ----
            {
                const float charNow = charSmooth.getCurrentValue();
                const float refNow  = juce::jmax (programmeRef.state * thresholdMul, thresholdFloor);
                const float trans   = clamp01 (envFollower.state / refNow - 1.0f);
                const float baseRel = 320.0f - 180.0f * charNow;            // vintage slow, modern tight
                const float rel     = juce::jlimit (80.0f, 400.0f, baseRel / (1.0f + 1.5f * trans));
                envFollower.setTimes (attackMs, rel);
            }

            float* data[2] { nullptr, nullptr };
            for (int ch = 0; ch < channels; ++ch)
                data[ch] = buffer.getWritePointer (ch);

            float blockGrDb = 0.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                const float tightAmt = tightSmooth.getNextValue();
                const float harmAmt  = harmSmooth.getNextValue();
                const float charAmt  = charSmooth.getNextValue();

                const float apAmt    = tightAmt;
                const float rumble   = juce::jmin (1.0f, tightAmt * (1.0f / 0.3f)); // full by tight ~0.3
                const float maxCutDb = maxCutRangeDb * tightAmt;

                // Vintage: soft curve, x^2-heavy (2nd). Modern: hotter tanh (3rd).
                const float drive    = 1.6f + 1.4f * charAmt;
                const float invDrive = 1.0f / drive;
                const float k2       = 0.7f - 0.45f * charAmt;

                float lowBand[2] {}, highBand[2] {}, harmBand[2] {};
                float detect = 0.0f;

                for (int ch = 0; ch < channels; ++ch)
                {
                    const float x = sanitize (data[ch][i]);

                    // Exact-recombine split: high is what the lowpass left behind.
                    const float low  = splitLp[ch].processLp (x);
                    const float high = x - low;

                    // Phase align, blended so tight==0 stays untouched.
                    float lowP = low + apAmt * (phaseAp[ch].process (low) - low);

                    // Rumble guard, crossfaded in with tight.
                    lowP += rumble * (rumbleHp[ch].processHp (lowP) - lowP);

                    // Harmonic bass: band-pass, soft rectifying curve,
                    // low-pass the products, block the DC the x^2 term makes.
                    const float band = harmLp[ch].processLp (harmHp[ch].processHp (x));
                    const float d    = band * drive;
                    float harm       = fastTanh (d + k2 * d * d) * invDrive;
                    harm             = dcBlock[ch].process (harmPostLp[ch].processLp (harm));

                    lowBand[ch]  = lowP;
                    highBand[ch] = high;
                    harmBand[ch] = harm;

                    detect = juce::jmax (detect, std::abs (lowP)); // stereo-linked
                }

                // ---- Dynamic low control: duck sustain above the programme --
                const float env = envFollower.processSample (detect);
                const float ref = programmeRef.processSample (env);
                const float thr = juce::jmax (ref * thresholdMul, thresholdFloor);

                float grTargetDb = 0.0f;
                if (maxCutDb > 1.0e-4f && env > thr)
                    grTargetDb = juce::jmin (maxCutDb, grSlope * gainToDb (env / thr));

                grDbState = flushDenormal (grTargetDb + grDezipCoef * (grDbState - grTargetDb));
                blockGrDb = juce::jmax (blockGrDb, grDbState);

                const float lowGain = dbToGain (-grDbState);
                const float harmMix = harmAmt * harmMixGain;

                for (int ch = 0; ch < channels; ++ch)
                    data[ch][i] = highBand[ch] + lowBand[ch] * lowGain + harmBand[ch] * harmMix;
            }

            grMeterDb.store (blockGrDb, std::memory_order_relaxed);
        }

        float getGainReductionDb() const { return grMeterDb.load (std::memory_order_relaxed); }

    private:
        // ---- Tiny per-channel filters (per-channel corners, flushed state) --
        struct TptSvf
        {
            void setCutoff (double sr, float hz, float q)
            {
                const float fc = juce::jlimit (5.0f, (float) (sr * 0.45), hz);
                const float g  = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
                k  = 1.0f / q;
                a1 = 1.0f / (1.0f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }

            float processLp (float x) { return tick (x).lp; }
            float processHp (float x) { const auto o = tick (x); return x - k * o.bp - o.lp; }

            void reset() { ic1 = ic2 = 0.0f; }

        private:
            struct Out { float lp, bp; };

            Out tick (float x)
            {
                const float v3 = x - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = flushDenormal (2.0f * v1 - ic1);
                ic2 = flushDenormal (2.0f * v2 - ic2);
                return { v2, v1 };
            }

            float k = 1.4142f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
            float ic1 = 0.0f, ic2 = 0.0f;
        };

        struct FirstOrderAllpass
        {
            void setCutoff (double sr, float hz)
            {
                const float fc = juce::jlimit (5.0f, (float) (sr * 0.45), hz);
                const float t  = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
                a = (t - 1.0f) / (t + 1.0f);
            }

            float process (float x)
            {
                const float y = a * x + x1 - a * y1;
                x1 = flushDenormal (x);
                y1 = flushDenormal (y);
                return y;
            }

            void reset() { x1 = y1 = 0.0f; }

            float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
        };

        struct DcBlocker
        {
            void setCutoff (double sr, float hz)
            {
                r = juce::jlimit (0.9f, 1.0f, 1.0f - juce::MathConstants<float>::twoPi * hz / (float) sr);
            }

            float process (float x)
            {
                const float y = x - x1 + r * y1;
                x1 = x;
                y1 = flushDenormal (y);
                return y;
            }

            void reset() { x1 = y1 = 0.0f; }

            float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
        };

        // ---- Voicing ---------------------------------------------------------
        static constexpr float splitHz        = 150.0f;   // low/high split
        static constexpr float phaseHz        = 90.0f;    // phase-align allpass
        static constexpr float rumbleHz       = 18.0f;    // rumble guard HP
        static constexpr float harmLoHz       = 40.0f;    // harmonic source band
        static constexpr float harmHiHz       = 120.0f;
        static constexpr float harmPostHz     = 300.0f;   // post-nonlinearity LP
        static constexpr float butterQ        = 0.70710678f;
        static constexpr float attackMs       = 15.0f;    // the transient window
        static constexpr float maxCutRangeDb  = 5.0f;     // full cut at tight==1
        static constexpr float grSlope        = 0.7f;     // soft over-threshold ratio
        static constexpr float thresholdMul   = 1.334f;   // ~+2.5 dB over reference
        static constexpr float thresholdFloor = 1.0e-4f;  // never duck silence
        static constexpr float harmMixGain    = 0.2f;     // ~-14 dB relative
        static constexpr float settleEps      = 1.0e-6f;

        double sampleRate = 48000.0;
        Params params;

        TptSvf            splitLp[2], rumbleHp[2], harmHp[2], harmLp[2], harmPostLp[2];
        FirstOrderAllpass phaseAp[2];
        DcBlocker         dcBlock[2];

        BallisticsFilter envFollower;   // low-band level, adaptive release
        BallisticsFilter programmeRef;  // slow auto-threshold reference

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> tightSmooth, harmSmooth, charSmooth;

        float grDbState   = 0.0f;       // stereo-linked, smoothed cut (positive dB)
        float grDezipCoef = 0.0f;
        bool  settled     = false;

        std::atomic<float> grMeterDb { 0.0f };
    };
} // namespace sauce::dsp
