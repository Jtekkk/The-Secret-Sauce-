#pragma once

#include "DspHelpers.h"

#include <atomic>

namespace sauce::dsp
{
    // =========================================================================
    // MicroDynamics — compression that doesn't sound like compression.
    //
    //   Glue    : slow feedback-style bus compressor, low ratio (1.4:1..2.5:1),
    //             auto makeup, blended in parallel (~50 %) so it reads as
    //             cohesion, not squash.
    //   Punch   : transient attack emphasis, low/mid weighted.
    //   Density : upward compression of low-level detail (max ~6 dB lift,
    //             gated so noise floors don't breathe up).
    //   Snap    : high-frequency transient emphasis.
    //
    // Topology notes:
    //   - One shared control level (extEnv from the sidechain Detector when
    //     provided, else an internal peak/RMS hybrid on the max of channels)
    //     feeds Glue and Density. Punch/Snap always detect from the audio
    //     itself so the emphasis lands exactly on the transients.
    //   - Glue's threshold is not a knob: a slow programme-level follower (dB
    //     domain, quick up / lazy down) rides the material and the threshold
    //     sits a few dB under it, so the compressor leans ~2-3 dB into gain
    //     reduction at any sensible level. The detector is feedback-flavoured
    //     (previous gain reduction is subtracted from the control level),
    //     which self-limits GR and gives the classic "settled" bus feel.
    //   - Release is adaptive everywhere: a transient-ness metric (fast minus
    //     slow follower on the control level) shortens release after brief
    //     peaks and lengthens it on sustained material — no fixed release, no
    //     pumping.
    //   - Punch/Snap split the (post glue/density) signal with a one-pole at
    //     ~5 kHz; the high band is derived by subtraction from the SAME
    //     signal, so low + high recombines exactly when both gains are 1.
    //   - Params::chanTimeMul skews per-channel envelope *timing* only
    //     (analog drift); every applied gain is stereo-linked (max GR,
    //     averaged lifts/shapes) so the image never wobbles.
    //   - Character: vintage = slower attack, softer knee, longer release and
    //     lazier transient hold; modern = faster, cleaner.
    //   - Null contract: active == false or all four amounts at 0 leaves the
    //     buffer bit-exact untouched once the (short) smoothing tails settle;
    //     a lightweight idle path keeps the programme follower warm without
    //     writing to the buffer.
    //   - Worst-case downward GR of the last block is published through
    //     getGainReductionDb() (positive dB, atomic, UI thread safe).
    // =========================================================================
    class MicroDynamics
    {
    public:
        struct Params
        {
            float glue      = 0.0f;   // 0..1
            float punch     = 0.0f;   // 0..1
            float density   = 0.0f;   // 0..1
            float snap      = 0.0f;   // 0..1
            float character = 1.0f;   // 0 = vintage .. 1 = modern
            float chanTimeMul[2] { 1.0f, 1.0f };
            bool  active    = true;
        };

        void prepare (double newSampleRate, int maxBlockSize, int numChannels)
        {
            sampleRate = newSampleRate > 1000.0 ? newSampleRate : 48000.0;
            juce::ignoreUnused (maxBlockSize, numChannels);

            // Internal control envelope mirrors the sidechain Detector so the
            // module behaves the same with or without extEnv.
            ctrlFast.prepare  (sampleRate, 0.1f,  40.0f);
            ctrlSlow.prepare  (sampleRate, 5.0f,  250.0f);
            transFast.prepare (sampleRate, 1.0f,  60.0f);
            transSlow.prepare (sampleRate, 40.0f, 400.0f);

            // ~5 kHz one-pole band split for Punch (low/mid) vs Snap (high).
            const double lpHz = juce::jmin (5000.0, 0.45 * sampleRate);
            lpCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * lpHz / sampleRate);

            // Sample-rate-only smoothing constants.
            progFall = coefForMs (1000.0f);   // programme level falls lazily
            makeupCf = coefForMs (400.0f);    // auto makeup drifts, never pumps
            transCf  = coefForMs (250.0f);    // transient-ness averaging
            shapeCf  = coefForMs (0.6f);      // de-clicks the transient gains

            glueSm.reset    (sampleRate, 0.03);
            punchSm.reset   (sampleRate, 0.03);
            densitySm.reset (sampleRate, 0.03);
            snapSm.reset    (sampleRate, 0.03);
            charSm.reset    (sampleRate, 0.05);
            glueSm.setCurrentAndTargetValue    (0.0f);
            punchSm.setCurrentAndTargetValue   (0.0f);
            densitySm.setCurrentAndTargetValue (0.0f);
            snapSm.setCurrentAndTargetValue    (0.0f);
            charSm.setCurrentAndTargetValue    (1.0f);

            cachedChr = -10.0f;               // force first refreshTimes()
            reset();
        }

        void reset()
        {
            ctrlFast.reset();
            ctrlSlow.reset();
            transFast.reset();
            transSlow.reset();

            grDb[0] = grDb[1] = 0.0f;
            liftDb[0] = liftDb[1] = 0.0f;
            lpState[0] = lpState[1] = 0.0f;
            prevGrDb = 0.0f;
            makeupDb = 0.0f;
            transAvg = 0.0f;
            progDb   = -60.0f;

            for (auto& b : bandLo) b.reset();
            for (auto& b : bandHi) b.reset();

            grMeter.store (0.0f, std::memory_order_relaxed);
        }

        void update (const Params& p) { params = p; }

        /** extEnv: optional per-sample detector envelope (numSamples long). */
        void process (juce::AudioBuffer<float>& buffer, int numSamples, const float* extEnv)
        {
            const int numCh = juce::jmin (buffer.getNumChannels(), 2);
            if (numSamples <= 0 || numCh <= 0)
                return;

            // Effective amounts: inactive == everything neutral, and the
            // smoothers turn any recipe move into a click-free ramp.
            const bool on = params.active;
            glueSm.setTargetValue    (on ? clamp01 (params.glue)    : 0.0f);
            punchSm.setTargetValue   (on ? clamp01 (params.punch)   : 0.0f);
            densitySm.setTargetValue (on ? clamp01 (params.density) : 0.0f);
            snapSm.setTargetValue    (on ? clamp01 (params.snap)    : 0.0f);
            charSm.setTargetValue    (clamp01 (params.character));

            const bool glueOn    = engaged (glueSm);
            const bool densityOn = engaged (densitySm);
            const bool transOn   = engaged (punchSm) || engaged (snapSm);

            if (! glueOn && ! densityOn && ! transOn)
            {
                trackIdle (buffer, numSamples, numCh, extEnv);   // bit-exact passthrough
                return;
            }

            // ---- Block-rate coefficient set --------------------------------
            const float chr = charSm.getCurrentValue();          // 0 vintage .. 1 modern
            charSm.skip (numSamples);
            const float slowMul = 1.0f + 0.5f * (1.0f - chr);    // vintage = lazier

            float tmul[2];
            tmul[0] = juce::jlimit (0.25f, 4.0f, params.chanTimeMul[0]);
            tmul[1] = juce::jlimit (0.25f, 4.0f, numCh > 1 ? params.chanTimeMul[1]
                                                           : params.chanTimeMul[0]);

            // Adaptive release: transient-rich material recovers fast (down to
            // ~100 ms), sustained material breathes long (up to ~600 ms,
            // vintage stretches further). GR ballistics smooth the changeover.
            const float relMs = juce::jlimit (80.0f, 900.0f,
                                              juce::jmap (clamp01 (transAvg), 600.0f, 100.0f) * slowMul);
            refreshTimes (chr, relMs, tmul);

            // Glue static curve (cheap; per-sample GR smoothing rides on top).
            const float glueNow  = glueSm.getCurrentValue();
            const float ratio    = juce::jmax (1.05f, 0.93f + 1.57f * glueNow); // 1.4:1 @ 0.3 .. 2.5:1 @ 1
            const float grSlope  = 1.0f - 1.0f / ratio;
            const float kneeDb   = 5.0f + 4.0f * (1.0f - chr);                  // ~6 dB, softer vintage
            const float offsetDb = 8.5f - 1.5f * glueNow;                       // rides ~2-3 dB into GR

            float* ch0 = buffer.getWritePointer (0);
            float* ch1 = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            float* chans[2] = { ch0, ch1 };

            float blockMaxGr = 0.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                const float gAmt = glueSm.getNextValue();
                const float pAmt = punchSm.getNextValue();
                const float dAmt = densitySm.getNextValue();
                const float sAmt = snapSm.getNextValue();

                // ---- Shared control level (linear) -------------------------
                float ctrl;
                if (extEnv != nullptr)
                {
                    ctrl = juce::jmax (0.0f, extEnv[i]);
                }
                else
                {
                    float det = std::abs (chans[0][i]);          // stereo-linked: max of channels
                    if (numCh > 1)
                        det = juce::jmax (det, std::abs (chans[1][i]));

                    const float f = ctrlFast.processSample (det);
                    const float s = ctrlSlow.processSample (det);
                    ctrl = juce::jmax (s, 0.6f * f + 0.4f * s);
                }

                const float ctrlDb = gainToDb (ctrl);

                // Transient-ness metric feeding next block's release choice.
                {
                    const float f = transFast.processSample (ctrl);
                    const float s = transSlow.processSample (ctrl);
                    const float inst = f > 1.0e-4f ? clamp01 ((f - s) / (s + 1.0e-6f)) : 0.0f;
                    transAvg = flushDenormal (inst + transCf * (transAvg - inst));
                }

                // Programme level (dB): quick up so the auto threshold finds
                // the material, lazy down so gaps don't drag it under.
                {
                    const float cf = ctrlDb > progDb ? times.progRise : progFall;
                    progDb = juce::jlimit (-120.0f, 80.0f, ctrlDb + cf * (progDb - ctrlDb));
                }
                const float threshDb = juce::jmax (progDb, -45.0f) - offsetDb;

                // ---- GLUE: feedback-flavoured soft-knee gain computer ------
                float grTarget = 0.0f;
                if (glueOn)
                {
                    const float over = (ctrlDb - prevGrDb) - threshDb;  // feedback: detect post-GR
                    if (over >= 0.5f * kneeDb)
                        grTarget = grSlope * over;
                    else if (over > -0.5f * kneeDb)
                    {
                        const float k = over + 0.5f * kneeDb;
                        grTarget = grSlope * k * k / (2.0f * kneeDb);
                    }
                    grTarget = juce::jmin (grTarget, 24.0f);
                }

                // Per-channel GR ballistics (drift-skewed timing), linked gain.
                float grLinked = 0.0f;
                for (int ch = 0; ch < numCh; ++ch)
                {
                    const float cf = grTarget > grDb[ch] ? times.grA[ch] : times.grR[ch];
                    grDb[ch] = grTarget + cf * (grDb[ch] - grTarget);
                    grLinked = juce::jmax (grLinked, grDb[ch]);
                }
                prevGrDb   = grLinked;
                blockMaxGr = juce::jmax (blockMaxGr, grLinked);

                // Auto makeup from average GR (partial, so loud stays loud-ish
                // instead of over-shooting on every release).
                const float mkTarget = 0.8f * grLinked;
                makeupDb = mkTarget + makeupCf * (makeupDb - mkTarget);

                // Parallel blend: w = 0.5*glue .. 0.7*glue.
                const float w = gAmt * (0.5f + 0.2f * gAmt);
                const float gluMul = 1.0f + w * (dbToGain (makeupDb - grLinked) - 1.0f);

                // ---- DENSITY: gated upward compression below the programme -
                const float liftMax = 6.0f * dAmt;
                const float gate    = clamp01 ((ctrlDb + 58.0f) * (1.0f / 6.0f)); // ~-55 dBFS gate
                const float liftTgt = juce::jlimit (0.0f, liftMax, 0.5f * (threshDb - ctrlDb)) * gate;

                float liftSum = 0.0f;
                for (int ch = 0; ch < numCh; ++ch)
                {
                    const float cf = liftTgt > liftDb[ch] ? times.liftA[ch] : times.liftR[ch];
                    liftDb[ch] = liftTgt + cf * (liftDb[ch] - liftTgt);
                    liftSum += liftDb[ch];
                }
                const float liftMul = dbToGain (numCh > 1 ? 0.5f * liftSum : liftSum);

                const float linkedMul = gluMul * liftMul;     // exactly 1 when both idle

                // ---- PUNCH / SNAP: two-band transient emphasis --------------
                if (transOn)
                {
                    float lo[2] { 0.0f, 0.0f }, hi[2] { 0.0f, 0.0f };
                    float shLo = 0.0f, shHi = 0.0f;

                    for (int ch = 0; ch < numCh; ++ch)
                    {
                        const float y = chans[ch][i] * linkedMul;
                        lpState[ch] = flushDenormal (y + lpCoef * (lpState[ch] - y));
                        lo[ch] = lpState[ch];
                        hi[ch] = y - lo[ch];                  // exact-sum complement

                        shLo += bandLo[ch].step (lo[ch], times.tfA[ch], times.tfR[ch],
                                                 times.tsA[ch], times.tsR[ch],
                                                 times.holdSmp[ch], times.decayCf[ch], shapeCf);
                        shHi += bandHi[ch].step (hi[ch], times.tfA[ch], times.tfR[ch],
                                                 times.tsA[ch], times.tsR[ch],
                                                 times.holdSmp[ch], times.decayCf[ch], shapeCf);
                    }

                    // Link the computed emphasis across channels; up to ~+4 dB
                    // per band, applied as a smooth linear boost (no distortion).
                    const float norm = numCh > 1 ? 0.5f : 1.0f;
                    const float gLo  = 1.0f + boostPerUnit * pAmt * shLo * norm;
                    const float gHi  = 1.0f + boostPerUnit * sAmt * shHi * norm;

                    for (int ch = 0; ch < numCh; ++ch)
                        chans[ch][i] = lo[ch] * gLo + hi[ch] * gHi;
                }
                else
                {
                    for (int ch = 0; ch < numCh; ++ch)
                        chans[ch][i] *= linkedMul;
                }
            }

            // Settle recursive state to exact neutral so the null contract is
            // met once amounts hit zero (and nothing denormalises in the tail).
            snapTiny (grDb[0]);   snapTiny (grDb[1]);
            snapTiny (liftDb[0]); snapTiny (liftDb[1]);
            snapTiny (makeupDb);  snapTiny (prevGrDb);

            grMeter.store (blockMaxGr, std::memory_order_relaxed);
        }

        float getGainReductionDb() const { return grMeter.load (std::memory_order_relaxed); }

    private:
        using Smoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

        static bool engaged (const Smoother& s) noexcept
        {
            return s.getCurrentValue() > 0.0f || s.getTargetValue() > 0.0f || s.isSmoothing();
        }

        static void snapTiny (float& v) noexcept
        {
            if (std::abs (v) < 1.0e-4f)
                v = 0.0f;
        }

        float coefForMs (float ms) const noexcept
        {
            if (ms <= 0.0f)
                return 0.0f;
            return (float) std::exp (-1.0 / (0.001 * (double) ms * sampleRate));
        }

        /** Fast-minus-slow rectified transient follower with peak hold and a
            short smoother so the resulting gain never steps. */
        struct BandShaper
        {
            float fastEnv = 0.0f, slowEnv = 0.0f, hold = 0.0f, shape = 0.0f;
            int   holdLeft = 0;

            float step (float bandSample,
                        float fA, float fR, float sA, float sR,
                        int holdSamples, float decayCoef, float shapeCoef) noexcept
            {
                const float a = std::abs (bandSample);

                fastEnv = flushDenormal (a + (a > fastEnv ? fA : fR) * (fastEnv - a));
                slowEnv = flushDenormal (a + (a > slowEnv ? sA : sR) * (slowEnv - a));

                // Normalised so detection is level-independent; gated so the
                // noise floor never reads as "transient".
                const float t = fastEnv > 1.0e-4f
                                    ? clamp01 ((fastEnv - slowEnv) / (slowEnv + 1.0e-6f))
                                    : 0.0f;

                if (t > hold)          { hold = t; holdLeft = holdSamples; }
                else if (holdLeft > 0) { --holdLeft; }
                else                   { hold = flushDenormal (hold * decayCoef); }

                shape = flushDenormal (hold + shapeCoef * (shape - hold));
                return shape;
            }

            void reset() noexcept { fastEnv = slowEnv = hold = shape = 0.0f; holdLeft = 0; }
        };

        /** All character / drift / release dependent one-pole coefficients,
            rebuilt only when their inputs actually move (bounds the exp()
            work even at 1-sample blocks). */
        void refreshTimes (float chr, float relMs, const float* tmul)
        {
            if (std::abs (chr - cachedChr) < 1.0e-3f
                && std::abs (relMs - cachedRel) < 2.0f
                && std::abs (tmul[0] - cachedT0) < 1.0e-3f
                && std::abs (tmul[1] - cachedT1) < 1.0e-3f)
                return;

            cachedChr = chr;  cachedRel = relMs;
            cachedT0 = tmul[0]; cachedT1 = tmul[1];

            const float vin      = 1.0f - chr;
            const float slowMul  = 1.0f + 0.5f * vin;
            const float attackMs = 10.0f + 20.0f * vin;      // glue: modern 10 ms, vintage 30 ms

            for (int ch = 0; ch < 2; ++ch)
            {
                const float m = tmul[ch];
                times.grA[ch]     = coefForMs (attackMs * m);
                times.grR[ch]     = coefForMs (relMs * m);
                times.liftA[ch]   = coefForMs (220.0f * slowMul * m);   // density swells in slowly
                times.liftR[ch]   = coefForMs (90.0f  * slowMul * m);   // ...and ducks out quicker
                times.tfA[ch]     = coefForMs ((0.3f + 0.9f * vin) * m);
                times.tfR[ch]     = coefForMs (25.0f * m);
                times.tsA[ch]     = coefForMs (15.0f * m);
                times.tsR[ch]     = coefForMs (160.0f * m);
                times.decayCf[ch] = coefForMs ((15.0f + 25.0f * vin) * m);
                times.holdSmp[ch] = juce::jmax (1, (int) std::lround (
                                        (3.0 + 9.0 * (double) vin) * 0.001 * sampleRate * (double) m));
            }

            times.progRise = coefForMs (250.0f * slowMul);
        }

        /** Fully idle: leave the buffer untouched (bit-exact null), but keep
            the programme follower warm at block rate and park all shaping
            state at neutral so re-engaging is smooth and instant. */
        void trackIdle (const juce::AudioBuffer<float>& buffer, int numSamples, int numCh,
                        const float* extEnv)
        {
            float peak = 0.0f;
            if (extEnv != nullptr)
                peak = estimatePeak (extEnv, numSamples);
            else
                for (int ch = 0; ch < numCh; ++ch)
                    peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));

            const float levelDb = gainToDb (peak);
            const float alpha   = 1.0f - (float) std::exp (-(double) numSamples / (0.4 * sampleRate));
            progDb = juce::jlimit (-120.0f, 80.0f, progDb + alpha * (levelDb - progDb));

            // Seed the followers to the current level and zero the shapers.
            ctrlFast.reset (peak);
            ctrlSlow.reset (peak);
            transFast.reset (peak);
            transSlow.reset (peak);

            grDb[0] = grDb[1] = 0.0f;
            liftDb[0] = liftDb[1] = 0.0f;
            prevGrDb = 0.0f;
            makeupDb = 0.0f;
            transAvg = 0.0f;

            for (auto& b : bandLo) b.reset();
            for (auto& b : bandHi) b.reset();

            for (int ch = 0; ch < numCh; ++ch)
                lpState[ch] = buffer.getReadPointer (ch)[numSamples - 1];

            charSm.setCurrentAndTargetValue (charSm.getTargetValue());
            grMeter.store (0.0f, std::memory_order_relaxed);
        }

        struct TimeSet
        {
            float grA[2] {}, grR[2] {};
            float liftA[2] {}, liftR[2] {};
            float tfA[2] {}, tfR[2] {}, tsA[2] {}, tsR[2] {};
            float decayCf[2] {};
            int   holdSmp[2] { 1, 1 };
            float progRise = 0.0f;
        };

        static constexpr float boostPerUnit = 0.585f;   // 1 + 0.585 ≈ +4 dB max emphasis

        double sampleRate = 48000.0;
        Params params;

        // Shared detection.
        BallisticsFilter ctrlFast, ctrlSlow;    // internal control envelope
        BallisticsFilter transFast, transSlow;  // transient-ness metric
        float transAvg = 0.0f;
        float progDb   = -60.0f;                // programme level follower (dB)

        // Glue / density state.
        float grDb[2] { 0.0f, 0.0f };           // per-channel smoothed GR (dB, >= 0)
        float prevGrDb = 0.0f;                  // linked GR fed back into the detector
        float makeupDb = 0.0f;
        float liftDb[2] { 0.0f, 0.0f };         // per-channel density lift (dB, >= 0)

        // Punch / snap state.
        float lpCoef = 0.0f;
        float lpState[2] { 0.0f, 0.0f };
        BandShaper bandLo[2], bandHi[2];

        // Coefficients.
        TimeSet times;
        float progFall = 0.0f, makeupCf = 0.0f, transCf = 0.0f, shapeCf = 0.0f;
        float cachedChr = -10.0f, cachedRel = -1.0f, cachedT0 = 0.0f, cachedT1 = 0.0f;

        // Parameter smoothing & metering.
        Smoother glueSm, punchSm, densitySm, snapSm, charSm;
        std::atomic<float> grMeter { 0.0f };
    };
} // namespace sauce::dsp
