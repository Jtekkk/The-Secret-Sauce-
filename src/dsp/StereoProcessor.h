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
            juce::ignoreUnused (maxBlockSize, numChannels);
            sampleRate = newSampleRate;

            // Fixed sheen corner (~9 kHz, kept safely below Nyquist).
            const double sheenHz = juce::jmin (9000.0, 0.4 * sampleRate);
            sheenCoef = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * sheenHz / sampleRate);

            // Mono-safe guard ballistics: ~200 ms energy window, ride the side
            // down in ~50 ms, let go over ~500 ms.
            rmsCoef  = 1.0f - (float) std::exp (-1.0 / (0.200 * sampleRate));
            guardAtk = (float) std::exp (-1.0 / (0.050 * sampleRate));
            guardRel = (float) std::exp (-1.0 / (0.500 * sampleRate));

            engage.reset      (sampleRate, 0.03);
            sideGain.reset    (sampleRate, 0.03);
            midGain.reset     (sampleRate, 0.03);
            sheenGain.reset   (sampleRate, 0.03);
            bassMonoMix.reset (sampleRate, 0.05);

            reset();
        }

        void reset()
        {
            engage.setCurrentAndTargetValue      (0.0f);
            sideGain.setCurrentAndTargetValue    (1.0f);
            midGain.setCurrentAndTargetValue     (1.0f);
            sheenGain.setCurrentAndTargetValue   (0.0f);
            bassMonoMix.setCurrentAndTargetValue (0.0f);

            bmFreqZ = 120.0f;
            retuneBassHp (bmFreqZ);
            clearAudioState();
        }

        void update (const Params& p) { params = p; }

        void process (juce::AudioBuffer<float>& buffer, int numSamples)
        {
            // Mono input: there is no side channel — leave the buffer alone.
            if (buffer.getNumChannels() < 2 || numSamples <= 0)
                return;

            // ---- Block-rate targets (recipe changes these every block) -----
            const float width = juce::jlimit (0.0f, 2.0f, sanitize (params.width));
            const float sheen = clamp01 (sanitize (params.sideSheen));
            const float bmHz  = juce::jlimit (0.0f, 300.0f, sanitize (params.bassMonoHz));
            const bool  bmOn  = bmHz > 0.5f;

            const bool engaged = params.active
                                  && (std::abs (width - 1.0f) > 1.0e-4f
                                      || sheen > 1.0e-4f
                                      || bmOn);

            engage.setTargetValue      (engaged ? 1.0f : 0.0f);
            sideGain.setTargetValue    (width);
            midGain.setTargetValue     (midCompensation (width));
            sheenGain.setTargetValue   (dbToGain (2.5f * sheen) - 1.0f);
            bassMonoMix.setTargetValue (bmOn ? 1.0f : 0.0f);

            // ---- Fully disengaged: bit-exact passthrough --------------------
            // The M/S round trip is only exact if we never take it; once the
            // engage fade has fully settled we skip processing entirely and
            // park the state so the next engage starts clean.
            if (! engage.isSmoothing() && engage.getCurrentValue() <= 0.0f)
            {
                snapSmoothers();
                clearAudioState();
                return;
            }

            // Bass-mono cutoff glides at block rate; the TDF2 biquad tolerates
            // the small per-block coefficient steps. The corner freezes while
            // disabled so enable/disable is only the (smoothed) crossfade.
            if (bmOn)
            {
                const float kBlock = 1.0f - (float) std::exp (-(double) numSamples / (0.08 * sampleRate));
                bmFreqZ += kBlock * (bmHz - bmFreqZ);
            }
            retuneBassHp (bmFreqZ);

            const bool safeOn = params.monoSafe;

            float* l = buffer.getWritePointer (0);
            float* r = buffer.getWritePointer (1);

            for (int i = 0; i < numSamples; ++i)
            {
                const float dryL = l[i];
                const float dryR = r[i];

                float m = (dryL + dryR) * 0.5f;
                float s = (dryL - dryR) * 0.5f;

                // Side sheen: parallel one-pole HF lift on S only.
                lpSheen += sheenCoef * (s - lpSheen);
                lpSheen  = flushDenormal (lpSheen);
                s += sheenGain.getNextValue() * (s - lpSheen);

                // Bass mono: Butterworth HP on S, crossfaded in/out.
                const float hp = processBassHp (s);
                s += bassMonoMix.getNextValue() * (hp - s);

                // Width, energy-compensated above 100 %.
                m *= midGain.getNextValue();
                s *= sideGain.getNextValue();

                // Mono safe: slow limiter on the S/M energy ratio. On normal
                // correlation material sSq stays under 0.81 * mSq and the
                // guard sits at unity, untouched.
                mSq = flushDenormal (mSq + rmsCoef * (m * m - mSq));
                sSq = flushDenormal (sSq + rmsCoef * (s * s - sSq));

                float guardTarget = 1.0f;
                if (safeOn && sSq > 0.81f * mSq + 1.0e-12f)
                    guardTarget = juce::jlimit (0.0f, 1.0f, 0.9f * std::sqrt (mSq / sSq));

                const float gc = guardTarget < guardGain ? guardAtk : guardRel;
                guardGain = flushDenormal (guardTarget + gc * (guardGain - guardTarget));
                s *= guardGain;

                const float wetL = m + s;
                const float wetR = m - s;

                // Engage fade keeps the neutral <-> processed hand-off silent.
                const float e = engage.getNextValue();
                l[i] = dryL + e * (wetL - dryL);
                r[i] = dryR + e * (wetR - dryR);
            }
        }

    private:
        /** Mid drop that keeps widening energy-neutral, soft-capped (tanh on
            the dB amount, knee ~3 dB) so extreme widths read wider, not
            quieter. Unity at width <= 1 — narrowing is plain S scaling. */
        static float midCompensation (float width)
        {
            if (width <= 1.0f)
                return 1.0f;

            const float comp   = 1.0f / std::sqrt (0.5f + 0.5f * width * width);
            const float dropDb = -gainToDb (comp);                    // > 0
            const float capped = 3.0f * std::tanh (dropDb * (1.0f / 3.0f));
            return dbToGain (-capped);
        }

        /** RBJ 2nd-order Butterworth high-pass, retuned at block rate. */
        void retuneBassHp (float freqHz)
        {
            const double f  = juce::jlimit (10.0, juce::jmin (300.0, 0.45 * sampleRate), (double) freqHz);
            const double w0 = juce::MathConstants<double>::twoPi * f / sampleRate;
            const double cw = std::cos (w0);
            const double al = std::sin (w0) / std::sqrt (2.0);        // Q = 1/sqrt(2)
            const double a0 = 1.0 + al;

            hpB0 = (float) ((1.0 + cw) * 0.5 / a0);
            hpB1 = (float) (-(1.0 + cw) / a0);
            hpB2 = hpB0;
            hpA1 = (float) (-2.0 * cw / a0);
            hpA2 = (float) ((1.0 - al) / a0);
        }

        float processBassHp (float x)
        {
            const float y = hpB0 * x + hpZ1;
            hpZ1 = flushDenormal (hpB1 * x - hpA1 * y + hpZ2);
            hpZ2 = flushDenormal (hpB2 * x - hpA2 * y);
            return y;
        }

        void snapSmoothers()
        {
            sideGain.setCurrentAndTargetValue    (sideGain.getTargetValue());
            midGain.setCurrentAndTargetValue     (midGain.getTargetValue());
            sheenGain.setCurrentAndTargetValue   (sheenGain.getTargetValue());
            bassMonoMix.setCurrentAndTargetValue (bassMonoMix.getTargetValue());
        }

        void clearAudioState()
        {
            lpSheen = 0.0f;
            hpZ1 = hpZ2 = 0.0f;
            mSq = sSq = 0.0f;
            guardGain = 1.0f;
        }

        using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

        double sampleRate = 48000.0;
        Params params;

        Smoothed engage, sideGain, midGain, sheenGain, bassMonoMix;

        // Side sheen (one-pole HP splitter) --------------------------------
        float sheenCoef = 0.7f, lpSheen = 0.0f;

        // Bass mono (TDF2 biquad on S) --------------------------------------
        float bmFreqZ = 120.0f;
        float hpB0 = 1.0f, hpB1 = 0.0f, hpB2 = 0.0f, hpA1 = 0.0f, hpA2 = 0.0f;
        float hpZ1 = 0.0f, hpZ2 = 0.0f;

        // Mono-safe correlation guard ---------------------------------------
        float rmsCoef = 0.0f, guardAtk = 0.0f, guardRel = 0.0f;
        float mSq = 0.0f, sSq = 0.0f, guardGain = 1.0f;
    };
} // namespace sauce::dsp
