#pragma once

#include "DspHelpers.h"

namespace sauce::dsp
{
    // =========================================================================
    // SaturationSuite — the analog colour engine. Runs INSIDE the oversampled
    // block (prepare() receives the worst-case oversampled rate; the rate the
    // block actually runs at arrives per block in Params::oversampledRate).
    //
    // Topology (series, each stage blended wet/dry by its own weight):
    //
    //   drive -> console -> tape -> tube -> transformer -> soft clip -> 1/drive
    //
    //   1. Console model (Gold/Silver/Crimson/Emerald/Titanium/Copper):
    //      bias-shifted tanh with a per-model drive/asymmetry recipe plus a
    //      gentle ±1 dB tilt around a 700 Hz pivot.
    //   2. Tape: tanh behind an exact-inverse pre/de-emphasis shelf pair at
    //      ~3 kHz so HF compresses first, like real tape. Vintage character
    //      raises the emphasis (more rounding), modern lowers it.
    //   3. Tube: bias-shifted tanh (2nd-harmonic bloom), DC-blocked.
    //   4. Transformer: lows split below ~160 Hz and saturated harder than the
    //      rest (3rd-harmonic thickness on bass), recombined, DC-blocked.
    //   5. Soft clip: cubic clipper x - x^3/3, hard-limited at ±2/3.
    //   6. Dynamic harmonics: a transient-weighted envelope (3 ms attack /
    //      80 ms release vs. a slow programme reference) pushes the tape drive
    //      up to +6 dB on hits so the colour breathes with the material.
    //
    // Implementation notes:
    //   - Every static curve uses first-order ADAA: the exact antiderivative
    //     ratio (F1(x) - F1(x1)) / (x - x1), falling back to the midpoint
    //     evaluation when the difference is ill-conditioned. Each stage owns
    //     its own x-history per channel; all drive/bias terms are folded into
    //     the shaper input so the history always sees the true sample stream.
    //   - Stages have unity small-signal gain, so blending a stage against its
    //     own input is level-sane and weight == 0 is exact passthrough.
    //   - Post-drive compensation is the exact reciprocal of the smoothed
    //     pre-gain: drive changes colour, not loudness (AutoGain downstream
    //     does the precise loudness match).
    //   - Rate-dependent coefficients are retuned whenever oversampledRate
    //     changes (no allocation); voicing morphs (character, console model)
    //     are smoothed at block rate, gains/weights per sample.
    //   - active == false fades to a bit-exact passthrough, then processing is
    //     skipped entirely and stage state is cleared.
    // =========================================================================
    class SaturationSuite
    {
    public:
        struct Params
        {
            float driveDb        = 0.0f;   // 0..~18 dB into the stages
            float tape           = 0.0f;   // 0..1 stage weights
            float tube           = 0.0f;
            float transformer    = 0.0f;
            float clip           = 0.0f;
            float dynHarm        = 0.0f;   // 0..1 dynamic harmonic amount
            int   console        = 0;      // 0=off, 1..6 = model index
            float consoleAmt     = 0.0f;   // 0..1 console stage intensity
            float character      = 1.0f;   // 0 = vintage .. 1 = modern
            float chanDriveMul[2] { 1.0f, 1.0f }; // analog drift skew
            double oversampledRate = 96000.0;     // actual rate process() runs at
            bool  active         = true;
        };

        void prepare (const juce::dsp::ProcessSpec& spec)
        {
            sampleRate = spec.sampleRate;
            updateForRate (juce::jmax (8000.0, spec.sampleRate));
            reset();
        }

        void reset()
        {
            resetStages();
            for (auto& st : chan)
                st.preGain.snapTo (1.0f);
            bypass.snapTo (0.0f);          // fade back in click-free
            characterSm = clamp01 (params.character);
            stagesCleared = true;
        }

        void update (const Params& p)
        {
            params = p;
            const double r = juce::jmax (8000.0, p.oversampledRate);
            if (! juce::exactlyEqual (r, currentRate))
                updateForRate (r);         // cheap retune, no allocation
        }

        /** Process at the oversampled rate. */
        void process (juce::dsp::AudioBlock<float> block)
        {
            const int numChannels = (int) juce::jmin (block.getNumChannels(), (size_t) maxCh);
            const int numSamples  = (int) block.getNumSamples();
            if (numChannels <= 0 || numSamples <= 0)
                return;

            const float activeTarget = params.active ? 1.0f : 0.0f;

            // Bypassed and the fade has settled: exact passthrough, cold state.
            if (activeTarget <= 0.0f && bypass.v <= 0.0f)
            {
                if (! stagesCleared)
                {
                    resetStages();
                    stagesCleared = true;
                }
                return;
            }
            stagesCleared = false;

            advanceVoicing (numSamples);

            // ---- Per-block targets (clamped so fuzzed Params stay finite) ---
            const float driveGain = juce::jmax (1.0e-4f,
                dbToGain (juce::jlimit (-24.0f, 24.0f, params.driveDb)));

            const float tapeT = clamp01 (params.tape);
            const float tubeT = clamp01 (params.tube);
            const float xfmrT = clamp01 (params.transformer);
            const float clipT = clamp01 (params.clip);
            const float dynT  = clamp01 (params.dynHarm);
            const float conT  = params.console > 0 ? clamp01 (params.consoleAmt) : 0.0f;

            float driveT[maxCh];
            for (int c = 0; c < maxCh; ++c)
                driveT[c] = driveGain * juce::jlimit (0.25f, 4.0f, params.chanDriveMul[c]);

            // ---- Sample loop ------------------------------------------------
            for (int i = 0; i < numSamples; ++i)
            {
                const float a     = bypass.step (activeTarget);
                const float wCon  = conW .step (conT);
                const float wTape = tapeW.step (tapeT);
                const float wTube = tubeW.step (tubeT);
                const float wXf   = xfmrW.step (xfmrT);
                const float wClip = clipW.step (clipT);
                const float dyn   = dynW .step (dynT);

                for (int c = 0; c < numChannels; ++c)
                {
                    auto& st = chan[c];
                    float* data = block.getChannelPointer ((size_t) c);

                    const float x0 = data[i];
                    const float g  = juce::jmax (1.0e-4f, st.preGain.step (driveT[c]));
                    float s = g * x0;

                    // -- Console: model-voiced asymmetric tanh + tone tilt ----
                    if (wCon > 0.0f)
                    {
                        float w = (st.conShaper.process (conDrive * s + conBias)
                                     - conBiasTanh) * conNorm;
                        w = st.dcConsole.process (w);
                        st.tiltLp = flushDenormal (st.tiltLp + tiltCoef * (w - st.tiltLp));
                        w = tiltLoGain * st.tiltLp + tiltHiGain * (w - st.tiltLp);
                        s += wCon * (w - s);
                    }

                    // -- Tape: pre-emphasis -> tanh -> exact de-emphasis ------
                    if (wTape > 0.0f)
                    {
                        float d = 1.0f;      // dynamic-harmonics drive, 1..2 (+6 dB)
                        if (dyn > 0.0f)
                        {
                            const float mag  = std::abs (x0);
                            const float fast = st.envFast.processSample (mag);
                            const float slow = st.envSlow.processSample (mag);
                            d += dyn * clamp01 ((fast - slow) / (slow + 0.02f));
                        }

                        float w = shelfForward (emph, st.emphFwd, s);
                        w = st.tapeShaper.process (d * w) / d;
                        w = shelfInverse (emph, st.emphInv, w);
                        s += wTape * (w - s);
                    }

                    // -- Tube: 2nd-harmonic bloom, DC-blocked -----------------
                    if (wTube > 0.0f)
                    {
                        float w = (st.tubeShaper.process (s + tubeBias)
                                     - tubeBiasTanh) * tubeNorm;
                        w = st.dcTube.process (w);
                        s += wTube * (w - s);
                    }

                    // -- Transformer: saturate the lows harder, recombine -----
                    if (wXf > 0.0f)
                    {
                        st.xfmrLp = flushDenormal (st.xfmrLp + xfmrCoef * (s - st.xfmrLp));
                        float w = (s - st.xfmrLp)
                                    + st.xfmrShaper.process (xfmrDrive * st.xfmrLp) * xfmrNorm;
                        w = st.dcXfmr.process (w);
                        s += wXf * (w - s);
                    }

                    // -- Cubic soft clip (safety / modern edge) ---------------
                    if (wClip > 0.0f)
                        s += wClip * (st.clipShaper.process (s) - s);

                    // -- Inverse drive compensation + bypass crossfade --------
                    const float out = s / g;
                    data[i] = x0 + a * (out - x0);
                }
            }
        }

    private:
        static constexpr int maxCh = 2;

        // ---------------------------------------------------------------------
        // Small building blocks
        // ---------------------------------------------------------------------

        /** log(cosh(x)) evaluated without overflow: |x| + log1p(e^{-2|x|}) - log 2. */
        static float logCosh (float x)
        {
            const float a = std::abs (x);
            return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718056f;
        }

        /** First-order ADAA tanh: exact antiderivative ratio with midpoint fallback. */
        struct AdaaTanh
        {
            float x1 = 0.0f, F1 = 0.0f;

            void reset() { x1 = 0.0f; F1 = 0.0f; }

            float process (float x)
            {
                const float F  = logCosh (x);
                const float dx = x - x1;
                const float y  = std::abs (dx) > 1.0e-4f
                                    ? (F - F1) / dx
                                    : std::tanh (0.5f * (x + x1));
                x1 = x;
                F1 = F;
                return y;
            }
        };

        /** Cubic soft clip: x - x^3/3 inside ±1, ±2/3 beyond. Piecewise-exact ADAA. */
        struct AdaaClip
        {
            float x1 = 0.0f, F1 = 0.0f;

            void reset() { x1 = 0.0f; F1 = 0.0f; }

            static float shape (float x)
            {
                if (x >  1.0f) return  2.0f / 3.0f;
                if (x < -1.0f) return -2.0f / 3.0f;
                return x - x * x * x * (1.0f / 3.0f);
            }

            static float antideriv (float x)
            {
                const float a = std::abs (x);
                if (a > 1.0f)
                    return (2.0f / 3.0f) * a - 0.25f;   // continuous at |x| = 1
                const float x2 = x * x;
                return 0.5f * x2 - x2 * x2 * (1.0f / 12.0f);
            }

            float process (float x)
            {
                const float F  = antideriv (x);
                const float dx = x - x1;
                const float y  = std::abs (dx) > 1.0e-4f
                                    ? (F - F1) / dx
                                    : shape (0.5f * (x + x1));
                x1 = x;
                F1 = F;
                return y;
            }
        };

        /** ~10 Hz one-pole DC blocker (HF gain normalised to unity). */
        struct DcBlocker
        {
            float x1 = 0.0f, y1 = 0.0f, R = 0.999f, g = 0.9995f;

            void set (double fs)
            {
                R = (float) std::exp (-juce::MathConstants<double>::twoPi * 10.0 / fs);
                g = 0.5f * (1.0f + R);
            }

            void reset() { x1 = 0.0f; y1 = 0.0f; }

            float process (float x)
            {
                const float y = g * (x - x1) + R * y1;
                x1 = x;
                y1 = flushDenormal (y);
                return y;
            }
        };

        /** One-pole parameter smoother in update-fraction form (k stays
            representable even at 16x-oversampled rates), with exact snapping
            so settled values null perfectly. */
        struct Smoother
        {
            float v = 0.0f, k = 1.0f;

            void setTau (double fs, double seconds)
            {
                k = (float) -std::expm1 (-1.0 / juce::jmax (1.0, fs * seconds));
            }

            void snapTo (float x) { v = x; }

            float step (float target)
            {
                v += k * (target - v);
                if (std::abs (v - target) < 1.0e-6f)
                    v = target;
                return v;
            }
        };

        /** First-order high shelf (bilinear, min-phase => exactly invertible).
            H(z) = (b0 + b1 z^-1) / (1 + a1 z^-1). */
        struct ShelfCoeffs
        {
            float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;

            void set (float hfGain, float w) // w = tan(pi * fc / fs)
            {
                const float d = 1.0f / (1.0f + w);
                b0 = (hfGain + w) * d;
                b1 = (w - hfGain) * d;
                a1 = (w - 1.0f) * d;
            }
        };

        struct ShelfState
        {
            float x1 = 0.0f, y1 = 0.0f;
            void reset() { x1 = 0.0f; y1 = 0.0f; }
        };

        static float shelfForward (const ShelfCoeffs& c, ShelfState& s, float x)
        {
            const float y = c.b0 * x + c.b1 * s.x1 - c.a1 * s.y1;
            s.x1 = x;
            s.y1 = flushDenormal (y);
            return y;
        }

        /** Exact inverse of shelfForward (pole at (G-w)/(G+w), always stable). */
        static float shelfInverse (const ShelfCoeffs& c, ShelfState& s, float x)
        {
            const float y = (x + c.a1 * s.x1 - c.b1 * s.y1) / c.b0;
            s.x1 = x;
            s.y1 = flushDenormal (y);
            return y;
        }

        /** Fixed harmonic recipe per console model: drive bias, asymmetry, tilt. */
        struct ConsoleModel { float drive, bias, tiltDb; };

        static const ConsoleModel& modelFor (int m)
        {
            static constexpr ConsoleModel models[7] = {
                { 1.00f, 0.00f,  0.00f },  // 0 off (weight is zero anyway)
                { 1.20f, 0.26f, -0.80f },  // 1 Gold     — warm, round
                { 0.90f, 0.10f,  0.80f },  // 2 Silver   — clean, airy
                { 1.60f, 0.22f, -0.30f },  // 3 Crimson  — hot, mid-forward
                { 1.25f, 0.16f, -1.00f },  // 4 Emerald  — thick lows
                { 0.70f, 0.05f,  0.40f },  // 5 Titanium — nearly clinical
                { 1.40f, 0.14f, -0.60f },  // 6 Copper   — transformer heft
            };
            return models[juce::jlimit (0, 6, m)];
        }

        // ---------------------------------------------------------------------
        // Per-channel state (one ADAA history per stage, never shared)
        // ---------------------------------------------------------------------
        struct ChannelState
        {
            Smoother   preGain;                 // drive incl. per-channel skew
            AdaaTanh   conShaper, tapeShaper, tubeShaper, xfmrShaper;
            AdaaClip   clipShaper;
            DcBlocker  dcConsole, dcTube, dcXfmr;
            ShelfState emphFwd, emphInv;        // tape pre-/de-emphasis
            float      tiltLp = 0.0f;           // console tilt pivot LP
            float      xfmrLp = 0.0f;           // transformer LF split
            BallisticsFilter envFast, envSlow;  // dynamic-harmonics detectors

            void reset()
            {
                conShaper.reset();  tapeShaper.reset(); tubeShaper.reset();
                xfmrShaper.reset(); clipShaper.reset();
                dcConsole.reset();  dcTube.reset();     dcXfmr.reset();
                emphFwd.reset();    emphInv.reset();
                tiltLp = 0.0f;
                xfmrLp = 0.0f;
                envFast.reset();
                envSlow.reset();
            }
        };

        // ---------------------------------------------------------------------
        // Rate / voicing housekeeping
        // ---------------------------------------------------------------------

        /** Retune everything that depends on the (oversampled) processing rate. */
        void updateForRate (double fs)
        {
            currentRate = fs;

            bypass.setTau (fs, 0.005);
            conW  .setTau (fs, 0.010);
            tapeW .setTau (fs, 0.010);
            tubeW .setTau (fs, 0.010);
            xfmrW .setTau (fs, 0.010);
            clipW .setTau (fs, 0.010);
            dynW  .setTau (fs, 0.010);

            const auto onePole = [fs] (double hz)
            {
                return (float) -std::expm1 (-juce::MathConstants<double>::twoPi * hz / fs);
            };

            tiltCoef = onePole (juce::jmin (700.0, 0.20 * fs));   // console pivot
            xfmrCoef = onePole (juce::jmin (160.0, 0.05 * fs));   // LF core split
            emphW    = (float) std::tan (juce::MathConstants<double>::pi
                                            * juce::jmin (3000.0, 0.35 * fs) / fs);

            for (auto& st : chan)
            {
                st.preGain.setTau (fs, 0.005);
                st.dcConsole.set (fs);
                st.dcTube.set (fs);
                st.dcXfmr.set (fs);

                // Retune ballistics without wiping their envelopes.
                st.envFast.sr = fs;  st.envFast.setTimes (3.0f,  80.0f);
                st.envSlow.sr = fs;  st.envSlow.setTimes (60.0f, 300.0f);
            }
        }

        /** Block-rate morph of curve voicings (character, console model) and
            the coefficients derived from them. Time-accurate for any block
            length; the resulting per-block coefficient steps are tiny. */
        void advanceVoicing (int numSamples)
        {
            const float alpha = (float) -std::expm1 (-(double) numSamples
                                                        / (0.05 * currentRate));

            characterSm += alpha * (clamp01 (params.character) - characterSm);

            if (params.console >= 1)
            {
                const auto& m = modelFor (params.console);
                conDriveSm += alpha * (m.drive  - conDriveSm);
                conBiasSm  += alpha * (m.bias   - conBiasSm);
                conTiltSm  += alpha * (m.tiltDb - conTiltSm);
            }
            // Console 0 keeps the last voicing — its weight fades to zero.

            // Console shaper: vintage keeps full asymmetry, modern halves it.
            conDrive    = juce::jmax (0.2f, conDriveSm);
            conBias     = conBiasSm * (1.0f - 0.5f * characterSm);
            conBiasTanh = std::tanh (conBias);
            conNorm     = 1.0f / (conDrive * (1.0f - conBiasTanh * conBiasTanh));
            tiltHiGain  = dbToGain ( conTiltSm);
            tiltLoGain  = dbToGain (-conTiltSm);

            // Tape emphasis: +3 dB @ ~3 kHz vintage .. +1 dB modern.
            emph.set (dbToGain (3.0f - 2.0f * characterSm), emphW);

            // Tube bias: vintage blooms the 2nd harmonic, modern stays tight.
            tubeBias     = 0.15f + 0.30f * (1.0f - characterSm);
            tubeBiasTanh = std::tanh (tubeBias);
            tubeNorm     = 1.0f / (1.0f - tubeBiasTanh * tubeBiasTanh);

            // Transformer core drive: vintage pushes the lows harder.
            xfmrDrive = 1.6f + 0.9f * (1.0f - characterSm);
            xfmrNorm  = 1.0f / xfmrDrive;
        }

        void resetStages()
        {
            for (auto& st : chan)
                st.reset();

            conW.snapTo (0.0f);  tapeW.snapTo (0.0f); tubeW.snapTo (0.0f);
            xfmrW.snapTo (0.0f); clipW.snapTo (0.0f); dynW.snapTo (0.0f);
        }

        // ---------------------------------------------------------------------
        double sampleRate  = 96000.0;   // worst-case rate given to prepare()
        double currentRate = 96000.0;   // rate process() actually runs at
        Params params;

        ChannelState chan[maxCh];

        // Per-sample smoothers shared across channels.
        Smoother bypass, conW, tapeW, tubeW, xfmrW, clipW, dynW;

        // Block-smoothed voicing + derived coefficients.
        float characterSm = 1.0f;
        float conDriveSm = 1.0f, conBiasSm = 0.0f, conTiltSm = 0.0f;
        float conDrive = 1.0f, conBias = 0.0f, conBiasTanh = 0.0f, conNorm = 1.0f;
        float tiltHiGain = 1.0f, tiltLoGain = 1.0f, tiltCoef = 0.05f;
        float tubeBias = 0.15f, tubeBiasTanh = 0.0f, tubeNorm = 1.0f;
        float xfmrDrive = 1.6f, xfmrNorm = 0.625f, xfmrCoef = 0.01f;
        float emphW = 0.1f;
        ShelfCoeffs emph;

        bool stagesCleared = true;
    };
} // namespace sauce::dsp
