#pragma once

#include "DspHelpers.h"

#include <array>
#include <complex>
#include <memory>
#include <vector>

namespace sauce::dsp
{
    // =========================================================================
    // MusicalEQ — six musical macros instead of a parametric EQ.
    //
    //   Weight   : 60 Hz low shelf (±4 dB) + subtle 40 Hz bell
    //   Warmth   : wide 200 Hz bell (±3 dB) + gentle downward tilt
    //   Body     : 400 Hz bell (±3 dB)
    //   Presence : 3.5 kHz bell (±3.5 dB)
    //   Sparkle  : 8 kHz bell + 16 kHz shelf blend
    //   Air      : 12 kHz high shelf (±4.5 dB)
    //
    // Requirements for implementers:
    //   - Minimum-phase path: analog-matched (decramped) coefficients — e.g.
    //     Vicanek matched biquads or Orfanidis peaking design — so high-shelf
    //     and HF bell curves keep their analog shape near Nyquist at 44.1 kHz
    //     instead of cramping. This is non-negotiable: Air/Sparkle live where
    //     bilinear-transform biquads are at their worst.
    //   - Linear-phase option: build a linear-phase FIR of the same composite
    //     magnitude response (FFT-sample the cascade magnitude, zero phase,
    //     window). Report group delay via latencySamples(). Rebuilds must not
    //     glitch or allocate on the audio thread mid-block (precompute /
    //     double-buffer; juce::dsp::Convolution's background loader is
    //     acceptable).
    //   - Character: vintage shifts centres down slightly and widens Q;
    //     modern is surgical.
    //   - Per-channel corner skew via Params::chanFreqMul (analog drift).
    //   - Coefficient updates smoothed/interpolated — sweeping any macro must
    //     be click-free and the filters unconditionally stable (44.1k..192k).
    //   - active==false must be a true bypass (no phase shift, no CPU beyond
    //     the check) once smoothing has settled.
    //
    // Implementation notes:
    //   - The six macros expand to nine biquad sections (Weight and Sparkle
    //     own two each, +Warmth adds a hidden 3 kHz counter-dip).
    //   - Matched design (Vicanek 2016 style): poles by impulse invariance of
    //     the analog prototype denominator (always inside the unit circle),
    //     zeros solved in the phi-basis so the digital magnitude equals the
    //     ANALOG prototype exactly at DC, at f0 and at Nyquist — no cramping.
    //   - Per-band gain (dB) runs through a ~30 ms one-pole at block rate;
    //     sections whose smoothed gain is below 0.05 dB drop out of the chain
    //     entirely, so neutral settings decay to bit-exact passthrough.
    //   - Linear phase: the composite ANALOG magnitude is sampled on an FFT
    //     grid (truly uncramped), zero phase, inverse FFT, fftshift + Hann.
    //     The IR is rebuilt at most every ~60 ms into preallocated storage
    //     and handed to juce::dsp::Convolution, whose background loader
    //     crossfades engines click-free. Until the first IR is live the
    //     minimum-phase path keeps running (never silence).
    // =========================================================================
    class MusicalEQ
    {
    public:
        struct Params
        {
            float warmth    = 0.0f;   // all -1..1
            float air       = 0.0f;
            float body      = 0.0f;
            float presence  = 0.0f;
            float weight    = 0.0f;
            float sparkle   = 0.0f;
            float character = 1.0f;   // 0 = vintage .. 1 = modern
            bool  linearPhase = false;
            float chanFreqMul[2] { 1.0f, 1.0f };
            bool  active    = true;
        };

        void prepare (double newSampleRate, int maxBlockSize, int numChannels)
        {
            sampleRate      = juce::jmax (8000.0, newSampleRate);
            maxBlock        = juce::jmax (1, maxBlockSize);
            numPrepChannels = juce::jlimit (1, maxChannels, numChannels);

            // Fixed FIR length per sample rate; FFT grid is the next pow2 up.
            if      (sampleRate <= 50000.0)  { firLen = 2047; fftOrder = 12; }
            else if (sampleRate <= 100000.0) { firLen = 4095; fftOrder = 13; }
            else                             { firLen = 8191; fftOrder = 14; }
            fftSize = 1 << fftOrder;

            fft = std::make_unique<juce::dsp::FFT> (fftOrder);
            fftIn .assign ((size_t) fftSize, { 0.0f, 0.0f });
            fftOut.assign ((size_t) fftSize, { 0.0f, 0.0f });

            window.resize ((size_t) firLen);
            for (int i = 0; i < firLen; ++i)
                window[(size_t) i] = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi
                                                           * (double) i / (double) (firLen - 1));

            for (auto& slot : irStorage)
                slot.setSize (numPrepChannels, firLen);

            scratch.setSize (numPrepChannels, maxBlock);

            convolution.prepare ({ sampleRate, (juce::uint32) maxBlock,
                                   (juce::uint32) numPrepChannels });

            linEngaged          = false;
            irBuilt             = false;
            irSlot              = 0;
            samplesSinceIrCheck = 1 << 30;   // first linear-phase block builds at once

            reset();
        }

        void reset()
        {
            // Snap every smoothed control to its target and rebuild the state
            // from scratch — used after transport jumps, so no old tails leak.
            smChar = (double) targetChar;
            for (int ch = 0; ch < maxChannels; ++ch)
                smFreqMul[ch] = (double) targetFreqMul[ch];

            dsgChar = smChar;
            for (int ch = 0; ch < maxChannels; ++ch)
                dsgFreqMul[ch] = smFreqMul[ch];

            anyBandEngaged = false;

            for (int b = 0; b < numBands; ++b)
            {
                smDb[b] = (double) targetDb[b];
                bandEngaged[b] = std::abs (smDb[b]) >= (double) gainSkipDb;
                anyBandEngaged = anyBandEngaged || bandEngaged[b];

                for (int ch = 0; ch < maxChannels; ++ch)
                {
                    biq[b][ch].s1 = 0.0;
                    biq[b][ch].s2 = 0.0;
                }

                if (bandEngaged[b])
                    designBand (b);
                else
                    dsgDb[b] = smDb[b];
            }

            convolution.reset();
            samplesSinceIrCheck = 1 << 30;
        }

        void update (const Params& p)
        {
            params = p;

            const float weight   = juce::jlimit (-1.0f, 1.0f, sanitize (p.weight));
            const float warmth   = juce::jlimit (-1.0f, 1.0f, sanitize (p.warmth));
            const float body     = juce::jlimit (-1.0f, 1.0f, sanitize (p.body));
            const float presence = juce::jlimit (-1.0f, 1.0f, sanitize (p.presence));
            const float sparkle  = juce::jlimit (-1.0f, 1.0f, sanitize (p.sparkle));
            const float air      = juce::jlimit (-1.0f, 1.0f, sanitize (p.air));

            targetDb[0] = 4.0f * weight;                                   // 60 Hz shelf
            targetDb[1] = 1.5f * weight;                                   // 40 Hz bell
            targetDb[2] = 3.0f * warmth;                                   // 200 Hz bell
            targetDb[3] = warmth > 0.0f ? -0.25f * (3.0f * warmth) : 0.0f; // 3 kHz counter-dip
            targetDb[4] = 3.0f * body;                                     // 400 Hz bell
            targetDb[5] = 3.5f * presence;                                 // 3.5 kHz bell
            targetDb[6] = 3.0f * sparkle;                                  // 8 kHz bell
            targetDb[7] = 1.5f * sparkle;                                  // 16 kHz shelf (half gain)
            targetDb[8] = 4.5f * air;                                      // 12 kHz shelf

            if (! params.active)
                for (float& g : targetDb)
                    g = 0.0f;

            targetChar = clamp01 (sanitize (p.character));
            for (int ch = 0; ch < maxChannels; ++ch)
            {
                const float m = sanitize (p.chanFreqMul[ch]);
                targetFreqMul[ch] = juce::jlimit (0.5f, 2.0f, m > 0.0f ? m : 1.0f);
            }

            // Linear-phase engagement flips only here, so latencySamples() is
            // stable between the host's latency check and process().
            if (params.linearPhase)
            {
                if (! linEngaged && irBuilt && convolution.getCurrentIRSize() >= firLen)
                {
                    linEngaged = true;
                    resetBandStates();    // biquads leave the circuit cleanly
                }
            }
            else if (linEngaged)
            {
                linEngaged = false;       // one-time phase switch, per contract
                convolution.reset();
                samplesSinceIrCheck = 1 << 30;
            }
        }

        void process (juce::AudioBuffer<float>& buffer, int numSamples)
        {
            const int nCh = juce::jmin (buffer.getNumChannels(), numPrepChannels);
            if (nCh <= 0 || numSamples <= 0)
                return;

            updateControls (numSamples);

            if (params.linearPhase && fft != nullptr)
                maintainImpulse (numSamples);

            if (linEngaged)
            {
                convolveInPlace (buffer, nCh, numSamples);
                return;
            }

            // Keep the convolver warm while the first IR loads so the switch
            // to linear phase happens with primed history, not a 20 ms hole.
            if (params.linearPhase && irBuilt)
                feedConvolutionDiscarding (buffer, nCh, numSamples);

            if (! anyBandEngaged)
                return;                          // bit-exact passthrough

            for (int ch = 0; ch < nCh; ++ch)
            {
                float* data = buffer.getWritePointer (ch);

                for (int b = 0; b < numBands; ++b)
                {
                    if (! bandEngaged[b])
                        continue;

                    auto& f = biq[b][ch];
                    const double b0 = f.b0, b1 = f.b1, b2 = f.b2, a1 = f.a1, a2 = f.a2;
                    double s1 = f.s1, s2 = f.s2;

                    for (int i = 0; i < numSamples; ++i)
                    {
                        const double x = (double) data[i];
                        const double y = b0 * x + s1;
                        s1 = b1 * x - a1 * y + s2;
                        s2 = b2 * x - a2 * y;
                        data[i] = (float) y;
                    }

                    f.s1 = flushSmall (s1);
                    f.s2 = flushSmall (s2);
                }
            }
        }

        /** Extra latency introduced by the linear-phase path (0 when min-phase). */
        int latencySamples() const { return linEngaged ? (firLen - 1) / 2 : 0; }

    private:
        //======================================================================
        static constexpr int maxChannels = 2;
        static constexpr int numBands    = 9;
        // Rotating preallocated IR storage. The convolver's background loader
        // copies a slot after we hand it over; 8 slots x >= 60 ms per rebuild
        // gives it >= ~480 ms before a slot is rewritten — comfortably beyond
        // any realistic loader stall (a torn IR would be an artefact, never
        // instability, but headroom is cheap).
        static constexpr int numIrSlots  = 8;

        static constexpr float  gainSkipDb   = 0.05f;   // below this a band is identity
        static constexpr float  redesignDb   = 0.01f;   // biquad recompute threshold
        static constexpr float  irRefreshDb  = 0.05f;   // IR rebuild threshold
        static constexpr double smoothingSec = 0.030;   // gain smoothing time constant
        static constexpr double irRebuildSec = 0.060;   // min interval between IR builds

        enum BandShape { shapeBell = 0, shapeLowShelf = 1, shapeHighShelf = 2 };

        /** width is Q for bells, RBJ slope S for shelves. */
        struct BandDef { int shape; float hz; float width; };

        static constexpr std::array<BandDef, numBands> bandDefs { {
            { shapeLowShelf,     60.0f, 0.90f },   // Weight shelf
            { shapeBell,         40.0f, 1.00f },   // Weight sub bell
            { shapeBell,        200.0f, 0.70f },   // Warmth
            { shapeBell,       3000.0f, 0.80f },   // Warmth counter-tilt
            { shapeBell,        400.0f, 0.90f },   // Body
            { shapeBell,       3500.0f, 0.80f },   // Presence
            { shapeBell,       8000.0f, 0.70f },   // Sparkle bell
            { shapeHighShelf, 16000.0f, 0.75f },   // Sparkle shelf
            { shapeHighShelf, 12000.0f, 0.70f },   // Air
        } };

        /** One TDF2 section, double precision (LF corners at 192 kHz are far
            too coefficient-sensitive for float). */
        struct Biquad
        {
            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
            double s1 = 0.0, s2 = 0.0;
        };

        static double flushSmall (double v) { return std::abs (v) < 1.0e-20 ? 0.0 : v; }
        static double sq (double v)         { return v * v; }

        static void smoothTowards (double& v, double target, double k)
        {
            v += (target - v) * k;
            if (std::abs (v - target) < 1.0e-4)
                v = target;
        }

        /** RBJ shelf slope -> Q (gain dependent). */
        static double shelfSlopeToQ (double S, double A)
        {
            const double d = (A + 1.0 / A) * (1.0 / juce::jmax (0.1, S) - 1.0) + 2.0;
            return 1.0 / std::sqrt (juce::jmax (1.0e-6, d));
        }

        /** |H(j w)|^2 of the analog prototype as a function of u = (f/f0)^2,
            with A = 10^(dB/40). Evaluated directly — this is the uncramped
            target for both the matched biquads and the linear-phase grid. */
        static double analogMag2 (int shape, double u, double A, double Q)
        {
            if (shape == shapeBell)
            {
                const double num = sq (1.0 - u) + u * sq (A / Q);
                const double den = sq (1.0 - u) + u * sq (1.0 / (A * Q));
                return num / den;
            }

            const double k = A / (Q * Q);
            if (shape == shapeLowShelf)
            {
                const double num = sq (A - u) + u * k;
                const double den = sq (1.0 - A * u) + u * k;
                return (A * A) * num / den;
            }

            const double num = sq (1.0 - A * u) + u * k;
            const double den = sq (A - u) + u * k;
            return (A * A) * num / den;
        }

        /** Vicanek-style matched biquad: poles via impulse invariance of the
            analog denominator (unconditionally stable for any f0/fs), zeros
            solved in the phi-basis so |H| matches the analog prototype
            exactly at DC, f0 and Nyquist. Writes coefficients only. */
        static void designMatched (int shape, double f0, double Q, double A,
                                   double fs, Biquad& out)
        {
            const auto identity = [&out]
            {
                out.b0 = 1.0; out.b1 = 0.0; out.b2 = 0.0; out.a1 = 0.0; out.a2 = 0.0;
            };

            if (! std::isfinite (f0) || ! std::isfinite (A) || ! std::isfinite (Q)
                || f0 <= 0.0 || A <= 0.0 || Q <= 1.0e-3)
            {
                identity();
                return;
            }

            const double w0 = juce::MathConstants<double>::twoPi * f0 / fs;

            // Pole natural frequency / damping of the analog denominator.
            double q, wp;
            if (shape == shapeBell)          { q = 1.0 / (2.0 * A * Q); wp = w0; }
            else if (shape == shapeLowShelf) { q = 1.0 / (2.0 * Q);     wp = w0 / std::sqrt (A); }
            else                             { q = 1.0 / (2.0 * Q);     wp = w0 * std::sqrt (A); }

            wp = juce::jmin (wp, juce::MathConstants<double>::pi * 0.995);
            q  = juce::jlimit (1.0e-3, 50.0, q);

            const double a1 = q <= 1.0
                ? -2.0 * std::exp (-q * wp) * std::cos  (std::sqrt (1.0 - q * q) * wp)
                : -2.0 * std::exp (-q * wp) * std::cosh (std::sqrt (q * q - 1.0) * wp);
            const double a2 = std::exp (-2.0 * q * wp);

            const double sn = std::sin (w0 * 0.5);
            const double p1 = sn * sn;
            const double p0 = 1.0 - p1;
            const double p2 = 4.0 * p0 * p1;

            const double A0 = sq (1.0 + a1 + a2);
            const double A1 = sq (1.0 - a1 + a2);
            const double A2 = -4.0 * a2;
            const double Dw0 = A0 * p0 + A1 * p1 + A2 * p2;

            const double uNy  = sq (0.5 * fs / f0);
            const double gDC2 = analogMag2 (shape, 0.0, A, Q);
            const double gNy2 = analogMag2 (shape, uNy, A, Q);
            const double gC2  = analogMag2 (shape, 1.0, A, Q);

            const double B0 = A0 * gDC2;
            const double B1 = A1 * gNy2;
            const double B2 = p2 > 1.0e-15 ? (Dw0 * gC2 - B0 * p0 - B1 * p1) / p2
                                           : A2;   // degenerate: fall back to unity shape

            const double sqB0 = std::sqrt (juce::jmax (0.0, B0));
            const double sqB1 = std::sqrt (juce::jmax (0.0, B1));
            const double W    = 0.5 * (sqB0 + sqB1);
            const double r    = std::sqrt (juce::jmax (0.0, W * W + B2));

            const double b0 = 0.5 * (W + r);
            const double b1 = 0.5 * (sqB0 - sqB1);
            const double b2 = W - b0;

            if (std::isfinite (b0) && std::isfinite (b1) && std::isfinite (b2)
                && std::isfinite (a1) && std::isfinite (a2) && std::abs (a2) < 1.0)
            {
                out.b0 = b0; out.b1 = b1; out.b2 = b2; out.a1 = a1; out.a2 = a2;
            }
            else
            {
                identity();
            }
        }

        //======================================================================
        // Block-rate control path: smoothing, band engagement, redesign.
        //======================================================================
        void updateControls (int numSamples)
        {
            const double k = 1.0 - std::exp (-(double) numSamples / (sampleRate * smoothingSec));

            smoothTowards (smChar, (double) targetChar, k);
            bool globalMoved = std::abs (smChar - dsgChar) > 1.0e-3;

            for (int ch = 0; ch < maxChannels; ++ch)
            {
                smoothTowards (smFreqMul[ch], (double) targetFreqMul[ch], k);
                globalMoved = globalMoved || std::abs (smFreqMul[ch] - dsgFreqMul[ch]) > 1.0e-4;
            }

            anyBandEngaged = false;

            for (int b = 0; b < numBands; ++b)
            {
                smoothTowards (smDb[b], (double) targetDb[b], k);

                const bool en = std::abs (smDb[b]) >= (double) gainSkipDb;

                if (en != bandEngaged[b])
                {
                    bandEngaged[b] = en;

                    if (en)
                    {
                        designBand (b);          // enters at ~0.05 dB: no click
                    }
                    else
                    {
                        for (int ch = 0; ch < maxChannels; ++ch)
                        {
                            biq[b][ch].s1 = 0.0;
                            biq[b][ch].s2 = 0.0;
                        }
                    }
                }
                else if (en && (globalMoved
                                || std::abs (smDb[b] - dsgDb[b]) > (double) redesignDb))
                {
                    designBand (b);
                }

                anyBandEngaged = anyBandEngaged || en;
            }

            if (globalMoved)
            {
                dsgChar = smChar;
                for (int ch = 0; ch < maxChannels; ++ch)
                    dsgFreqMul[ch] = smFreqMul[ch];
            }
        }

        /** Recompute one band's matched coefficients from the smoothed
            controls (both channels; states untouched). */
        void designBand (int b)
        {
            const auto& def = bandDefs[(size_t) b];

            const double A = std::pow (10.0, smDb[b] / 40.0);
            const double freqScale = 0.85 + 0.15 * smChar;          // vintage drops centres
            const double qScale    = (1.0 / 1.3) + (1.0 - 1.0 / 1.3) * smChar; // ..and widens

            const double Q = (def.shape == shapeBell
                                ? (double) def.width
                                : shelfSlopeToQ ((double) def.width, A)) * qScale;

            for (int ch = 0; ch < maxChannels; ++ch)
            {
                double f0 = (double) def.hz * freqScale * smFreqMul[ch];
                f0 = juce::jlimit (5.0, 0.45 * sampleRate, f0);     // never near-Nyquist bells
                designMatched (def.shape, f0, Q, A, sampleRate, biq[b][ch]);
            }

            dsgDb[b] = smDb[b];
        }

        void resetBandStates()
        {
            for (int b = 0; b < numBands; ++b)
                for (int ch = 0; ch < maxChannels; ++ch)
                {
                    biq[b][ch].s1 = 0.0;
                    biq[b][ch].s2 = 0.0;
                }
        }

        //======================================================================
        // Linear-phase path.
        //======================================================================
        void maintainImpulse (int numSamples)
        {
            samplesSinceIrCheck += numSamples;
            const int interval = (int) (sampleRate * irRebuildSec);

            if (irBuilt && samplesSinceIrCheck < interval)
                return;

            if (irBuilt && ! impulseNeedsRefresh())
            {
                samplesSinceIrCheck = 0;
                return;
            }

            buildAndLoadImpulse();
            samplesSinceIrCheck = 0;
        }

        bool impulseNeedsRefresh() const
        {
            if (std::abs (smChar - irChar) > 1.0e-3)
                return true;

            for (int ch = 0; ch < numPrepChannels; ++ch)
                if (std::abs (smFreqMul[ch] - irFreqMul[ch]) > 1.0e-4)
                    return true;

            for (int b = 0; b < numBands; ++b)
            {
                if (bandEngaged[b] != (std::abs (irDb[b]) >= (double) gainSkipDb))
                    return true;
                if (std::abs (smDb[b] - irDb[b]) > (double) irRefreshDb)
                    return true;
            }

            return false;
        }

        /** Sample the composite ANALOG magnitude on the FFT grid, zero phase,
            inverse FFT, fftshift, Hann window — all in preallocated buffers —
            then hand the IR to the convolver's background loader. */
        void buildAndLoadImpulse()
        {
            const int half = fftSize / 2;
            const double freqScale = 0.85 + 0.15 * smChar;
            const double qScale    = (1.0 / 1.3) + (1.0 - 1.0 / 1.3) * smChar;

            auto& slot = irStorage[(size_t) irSlot];

            for (int ch = 0; ch < numPrepChannels; ++ch)
            {
                // Snapshot the engaged bands' analog specs for this channel.
                int    shp[numBands];
                double As[numBands], Qs[numBands], invF0sq[numBands];
                int count = 0;

                for (int b = 0; b < numBands; ++b)
                {
                    if (! bandEngaged[b])
                        continue;

                    const auto& def = bandDefs[(size_t) b];
                    const double A = std::pow (10.0, smDb[b] / 40.0);
                    double f0 = (double) def.hz * freqScale * smFreqMul[ch];
                    f0 = juce::jlimit (5.0, 0.45 * sampleRate, f0);

                    shp[count]     = def.shape;
                    As[count]      = A;
                    Qs[count]      = (def.shape == shapeBell
                                        ? (double) def.width
                                        : shelfSlopeToQ ((double) def.width, A)) * qScale;
                    invF0sq[count] = 1.0 / (f0 * f0);
                    ++count;
                }

                for (int k = 0; k <= half; ++k)
                {
                    const double f  = sampleRate * (double) k / (double) fftSize;
                    const double ff = f * f;

                    double m2 = 1.0;
                    for (int i = 0; i < count; ++i)
                        m2 *= analogMag2 (shp[i], ff * invF0sq[i], As[i], Qs[i]);

                    const float m = (float) std::sqrt (m2);   // zero-phase, real-even
                    fftIn[(size_t) k] = { m, 0.0f };
                    if (k > 0 && k < half)
                        fftIn[(size_t) (fftSize - k)] = { m, 0.0f };
                }

                fft->perform (fftIn.data(), fftOut.data(), true);   // includes 1/N

                float* dst = slot.getWritePointer (ch);
                const int centre = (firLen - 1) / 2;

                for (int i = 0; i < firLen; ++i)
                {
                    int idx = i - centre;
                    if (idx < 0)
                        idx += fftSize;

                    dst[i] = (float) ((double) fftOut[(size_t) idx].real()
                                      * window[(size_t) i]);
                }
            }

            irChar = smChar;
            for (int ch = 0; ch < maxChannels; ++ch)
                irFreqMul[ch] = smFreqMul[ch];
            for (int b = 0; b < numBands; ++b)
                irDb[b] = smDb[b];

            // Hand a non-owning view to the wait-free loader; the storage slot
            // itself stays preallocated and is only reused numIrSlots builds
            // (>= 240 ms) later, long after the background copy completed.
            juce::AudioBuffer<float> view (slot.getArrayOfWritePointers(),
                                           numPrepChannels, firLen);
            convolution.loadImpulseResponse (std::move (view), sampleRate,
                                             numPrepChannels > 1
                                                 ? juce::dsp::Convolution::Stereo::yes
                                                 : juce::dsp::Convolution::Stereo::no,
                                             juce::dsp::Convolution::Trim::no,
                                             juce::dsp::Convolution::Normalise::no);

            irSlot  = (irSlot + 1) % numIrSlots;
            irBuilt = true;
        }

        void convolveInPlace (juce::AudioBuffer<float>& buffer, int nCh, int numSamples)
        {
            float* const* chans = buffer.getArrayOfWritePointers();
            int done = 0;

            while (done < numSamples)   // chunk defensively to the prepared max
            {
                const int len = juce::jmin (maxBlock, numSamples - done);
                juce::dsp::AudioBlock<float> block (chans, (size_t) nCh,
                                                    (size_t) done, (size_t) len);
                juce::dsp::ProcessContextReplacing<float> ctx (block);
                convolution.process (ctx);
                done += len;
            }
        }

        /** Run the convolver on a scratch copy (output discarded) so its
            history is primed when the linear-phase path takes over. */
        void feedConvolutionDiscarding (const juce::AudioBuffer<float>& buffer,
                                        int nCh, int numSamples)
        {
            int done = 0;

            while (done < numSamples)
            {
                const int len = juce::jmin (maxBlock, numSamples - done);
                for (int ch = 0; ch < nCh; ++ch)
                    scratch.copyFrom (ch, 0, buffer, ch, done, len);

                juce::dsp::AudioBlock<float> block (scratch.getArrayOfWritePointers(),
                                                    (size_t) nCh, 0, (size_t) len);
                juce::dsp::ProcessContextReplacing<float> ctx (block);
                convolution.process (ctx);
                done += len;
            }
        }

        //======================================================================
        double sampleRate      = 48000.0;
        int    maxBlock        = 512;
        int    numPrepChannels = 2;
        Params params;

        // Targets (from update) and block-rate smoothed controls.
        float  targetDb[numBands] {};
        float  targetChar = 1.0f;
        float  targetFreqMul[maxChannels] { 1.0f, 1.0f };

        double smDb[numBands] {};
        double smChar = 1.0;
        double smFreqMul[maxChannels] { 1.0, 1.0 };

        // Last-designed snapshots (redesign thresholds).
        double dsgDb[numBands] {};
        double dsgChar = 1.0;
        double dsgFreqMul[maxChannels] { 1.0, 1.0 };

        bool   bandEngaged[numBands] {};
        bool   anyBandEngaged = false;
        Biquad biq[numBands][maxChannels];

        // Linear-phase machinery (all storage sized in prepare()).
        int  firLen = 2047, fftOrder = 12, fftSize = 4096;
        bool linEngaged = false, irBuilt = false;
        int  irSlot = 0, samplesSinceIrCheck = 0;

        double irDb[numBands] {};
        double irChar = 1.0;
        double irFreqMul[maxChannels] { 1.0, 1.0 };

        std::unique_ptr<juce::dsp::FFT>         fft;
        std::vector<juce::dsp::Complex<float>>  fftIn, fftOut;
        std::vector<double>                     window;
        std::array<juce::AudioBuffer<float>, numIrSlots> irStorage;
        juce::AudioBuffer<float>                scratch;
        juce::dsp::Convolution                  convolution { juce::dsp::Convolution::Latency { 0 } };
    };
} // namespace sauce::dsp
