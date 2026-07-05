#pragma once

#include "DspHelpers.h"

#include <atomic>
#include <vector>

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
    // Topology (as implemented):
    //   - Drive: amount → 0..+9 dB in, matching -0..9 dB trim out, both
    //     smoothed. Loudness comes from density (clip + limit), not level;
    //     the chain's AutoGain does the exact loudness matching.
    //   - Clipper: identity below ~3 dB over the running programme level,
    //     tanh blend above it (blend ≈ amount * 0.6, vintage a touch more).
    //   - Limiter: allowed gain = (ceiling - 0.3 dB guard) / sidechain peak,
    //     running minimum over the lookahead window via a monotonic wedge,
    //     one-pole attack (τ ≈ lookahead/3) in the attenuation direction,
    //     adaptive 40–400 ms release (short after isolated peaks, long on
    //     dense material, vintage slower). Stereo-linked single gain.
    //   - Sidechain folds in a parabolic inter-sample peak *estimate* (honest
    //     label: not a certified ITU true-peak detector).
    //   - Transient preserve: fast-minus-slow detector lifts the threshold by
    //     up to ~1.5 dB on attacks so the front edge survives (0 = brickwall).
    //   - amount==0 (or active==false): once the smoothers settle, process()
    //     takes a pure delayed-copy path — bit-exact for the chain null test.
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

            // Monotonic wedge over the lookahead window (min of allowed gain).
            window = lookahead + 1;
            wedgeIdx.assign ((size_t) window, 0);
            wedgeVal.assign ((size_t) window, 1.0f);
            wedgeHead = 0;
            wedgeCount = 0;
            wedgeTime = 0;

            // Attack reaches ~95 % of the required attenuation within the
            // lookahead (3 time constants), so the gain is down before the
            // peak arrives at the delayed output.
            attackCoef  = (float) std::exp (-3.0 / (double) lookahead);
            releaseCoef = coefForMs (150.0f);
            densityCoef = coefForMs (250.0f);

            programmeEnv.prepare (sampleRate, 5.0f, 250.0f);   // running programme level
            fastEnv.prepare (sampleRate, 0.2f, 30.0f);         // transient: fast
            slowEnv.prepare (sampleRate, 15.0f, 150.0f);       // transient: slow

            amountSm.reset (sampleRate, 0.03);
            driveSm.reset (sampleRate, 0.03);
            trimSm.reset (sampleRate, 0.03);
            thresholdSm.reset (sampleRate, 0.02);
            characterSm.reset (sampleRate, 0.05);
            preserveSm.reset (sampleRate, 0.05);

            amountSm.setCurrentAndTargetValue (0.0f);
            driveSm.setCurrentAndTargetValue (1.0f);
            trimSm.setCurrentAndTargetValue (1.0f);
            thresholdSm.setCurrentAndTargetValue (dbToGain (-1.0f - interSampleGuardDb));
            characterSm.setCurrentAndTargetValue (1.0f);
            preserveSm.setCurrentAndTargetValue (0.7f);

            gainEnv = 1.0f;
            densityEnv = 0.0f;
            peak1 = peak2 = 0.0f;
            maxGrDb.store (0.0f, std::memory_order_relaxed);
        }

        void reset()
        {
            delay.clear();
            writePos = 0;

            wedgeHead = 0;
            wedgeCount = 0;
            wedgeTime = 0;

            programmeEnv.reset();
            fastEnv.reset();
            slowEnv.reset();

            amountSm.setCurrentAndTargetValue (amountSm.getTargetValue());
            driveSm.setCurrentAndTargetValue (driveSm.getTargetValue());
            trimSm.setCurrentAndTargetValue (trimSm.getTargetValue());
            thresholdSm.setCurrentAndTargetValue (thresholdSm.getTargetValue());
            characterSm.setCurrentAndTargetValue (characterSm.getTargetValue());
            preserveSm.setCurrentAndTargetValue (preserveSm.getTargetValue());

            gainEnv = 1.0f;
            densityEnv = 0.0f;
            peak1 = peak2 = 0.0f;
            maxGrDb.store (0.0f, std::memory_order_relaxed);
        }

        void update (const Params& p) { params = p; }

        void process (juce::AudioBuffer<float>& buffer, int numSamples)
        {
            const int cap = delay.getNumSamples();
            const int channels = juce::jmin (buffer.getNumChannels(),
                                             delay.getNumChannels(), maxChannels);
            if (numSamples <= 0 || channels <= 0)
                return;

            // ---- Block-rate targets (the recipe repaints Params every block).
            // Everything is sanitised/clamped so any fuzzed combination stays finite.
            const float amountT   = params.active ? clamp01 (sanitize (params.amount)) : 0.0f;
            const float driveDb   = 9.0f * amountT;
            const float ceilDb    = juce::jlimit (-60.0f, 0.0f, sanitize (params.ceilingDb));
            const float charT     = clamp01 (sanitize (params.character));
            const float preserveT = clamp01 (sanitize (params.transientPreserve));

            amountSm.setTargetValue (amountT);
            driveSm.setTargetValue (dbToGain (driveDb));
            trimSm.setTargetValue (dbToGain (-driveDb));     // matching trim: density, not level
            thresholdSm.setTargetValue (dbToGain (ceilDb - interSampleGuardDb));
            characterSm.setTargetValue (charT);
            preserveSm.setTargetValue (preserveT);

            // Adaptive release: short after isolated peaks, long on dense
            // material; vintage leans slower.
            const float density = clamp01 (densityEnv + 0.3f * (1.0f - charT));
            releaseCoef = coefForMs (juce::jmap (density, 40.0f, 400.0f));

            float* io  [maxChannels];
            float* ring[maxChannels];
            for (int ch = 0; ch < channels; ++ch)
            {
                io[ch]   = buffer.getWritePointer (ch);
                ring[ch] = delay.getWritePointer (ch);
            }

            // ---- amount==0, everything settled: bit-exact delayed copy ------
            // (Latency contract: the delay stays in the path; only the gain
            // work is skipped. Detector state keeps tracking so re-engaging
            // is click-free.)
            if (amountT <= 0.0f && juce::exactlyEqual (gainEnv, 1.0f)
                && ! amountSm.isSmoothing() && juce::exactlyEqual (amountSm.getCurrentValue(), 0.0f)
                && ! driveSm.isSmoothing()  && juce::exactlyEqual (driveSm.getCurrentValue(), 1.0f)
                && ! trimSm.isSmoothing()   && juce::exactlyEqual (trimSm.getCurrentValue(), 1.0f))
            {
                thresholdSm.setCurrentAndTargetValue (thresholdSm.getTargetValue());
                characterSm.setCurrentAndTargetValue (charT);
                preserveSm.setCurrentAndTargetValue (preserveT);
                wedgeHead = 0;
                wedgeCount = 0;

                int w = writePos;
                for (int i = 0; i < numSamples; ++i)
                {
                    int r = w - lookahead;
                    if (r < 0) r += cap;

                    float peak = 0.0f;
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        const float x = io[ch][i];
                        ring[ch][w] = x;
                        peak = juce::jmax (peak, std::abs (x));
                        io[ch][i] = ring[ch][r];
                    }

                    peak = sanitize (peak);
                    programmeEnv.processSample (peak);
                    fastEnv.processSample (peak);
                    slowEnv.processSample (peak);
                    peak2 = peak1;
                    peak1 = peak;

                    if (++w >= cap) w = 0;
                }

                writePos = w;
                densityEnv = flushDenormal (densityEnv * std::pow (densityCoef, (float) numSamples));
                maxGrDb.store (0.0f, std::memory_order_relaxed);
                return;
            }

            // ---- Engaged path ------------------------------------------------
            const int wedgeCap = (int) wedgeVal.size();
            int w = writePos;
            juce::int64 n = wedgeTime;
            float minGain = 1.0f;
            float xd[maxChannels];

            for (int i = 0; i < numSamples; ++i)
            {
                const float amountNow = amountSm.getNextValue();
                const float driveG    = driveSm.getNextValue();
                const float trimG     = trimSm.getNextValue();
                const float thrNow    = thresholdSm.getNextValue();
                const float charNow   = characterSm.getNextValue();
                const float presNow   = preserveSm.getNextValue();

                // Engagement fades with amount so entering/leaving the settled
                // bypass path is click-free (wet scales the sidechain, so the
                // limiter melts to unity instead of snapping off).
                const float wet = juce::jmin (1.0f, amountNow * 8.0f);

                // -- Drive ------------------------------------------------------
                float peak = 0.0f;
                for (int ch = 0; ch < channels; ++ch)
                {
                    xd[ch] = io[ch][i] * driveG;
                    peak = juce::jmax (peak, std::abs (xd[ch]));
                }
                peak = sanitize (peak);

                // -- Stage 1: soft clip, engaging ~3 dB above programme level ----
                const float progLvl = programmeEnv.processSample (peak);
                const float clipThr = juce::jmax (progLvl * 1.4125f,      // +3 dB
                                                  thrNow * 0.35f, 1.0e-4f);
                const float blend   = amountNow * (0.6f + 0.15f * (1.0f - charNow));

                float scPeak = peak;
                if (blend > 1.0e-6f)
                {
                    scPeak = 0.0f;
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        const float u = std::abs (xd[ch]);
                        if (u > clipThr)
                        {
                            // Identity below the knee, tanh above; saturates
                            // ~6 dB over the knee — shaves only the spiky top.
                            const float c = std::copysign (
                                clipThr * (1.0f + fastTanh (u / clipThr - 1.0f)), xd[ch]);
                            xd[ch] += blend * (c - xd[ch]);
                        }
                        scPeak = juce::jmax (scPeak, std::abs (xd[ch]));
                    }
                    scPeak = sanitize (scPeak);
                }

                // -- Transient detector (fast minus slow envelope) ----------------
                const float fastL = fastEnv.processSample (scPeak);
                const float slowL = slowEnv.processSample (scPeak);
                const float trans = clamp01 ((fastL - 1.25f * slowL)
                                             / (0.75f * slowL + 1.0e-6f));

                // -- Sidechain: one-sample-late peak + parabolic inter-sample
                //    estimate on local maxima (true-peak-ish guard).
                float sc = peak1;
                if (peak1 > peak2 && peak1 >= scPeak)
                {
                    const float denom = peak2 - 2.0f * peak1 + scPeak;
                    if (denom < -1.0e-9f)
                    {
                        const float d = scPeak - peak2;
                        sc = juce::jmin (peak1 - d * d / (8.0f * denom),
                                         peak1 * 1.334f);   // sane cap ~ +2.5 dB
                    }
                }
                peak2 = peak1;
                peak1 = scPeak;

                // -- Allowed gain; transients may poke ~1.5 dB into the clipper --
                const float allowed = thrNow * (1.0f + 0.1885f * presNow * trans);
                const float scw = sc * wet;
                float a = 1.0f;
                if (scw > allowed)
                    a = allowed / scw;

                // -- Running minimum via monotonic wedge (O(1) amortised) --------
                while (wedgeCount > 0
                       && wedgeIdx[(size_t) wedgeHead] <= n - (juce::int64) window)
                {
                    if (++wedgeHead >= wedgeCap) wedgeHead = 0;
                    --wedgeCount;
                }

                while (wedgeCount > 0)
                {
                    int back = wedgeHead + wedgeCount - 1;
                    if (back >= wedgeCap) back -= wedgeCap;
                    if (wedgeVal[(size_t) back] < a)
                        break;
                    --wedgeCount;
                }

                if (wedgeCount >= wedgeCap)   // unreachable by construction; safety
                {
                    if (++wedgeHead >= wedgeCap) wedgeHead = 0;
                    --wedgeCount;
                }

                {
                    int slot = wedgeHead + wedgeCount;
                    if (slot >= wedgeCap) slot -= wedgeCap;
                    wedgeIdx[(size_t) slot] = n;
                    wedgeVal[(size_t) slot] = a;
                    ++wedgeCount;
                }

                const float raw = wedgeCount > 0 ? wedgeVal[(size_t) wedgeHead] : 1.0f;

                // -- Gain envelope: fast one-pole down (τ ≈ lookahead/3), so the
                //    attenuation lands before the peak; adaptive release up.
                gainEnv = raw < gainEnv ? raw + attackCoef  * (gainEnv - raw)
                                        : raw + releaseCoef * (gainEnv - raw);
                if (raw >= 1.0f && gainEnv > 0.9999994f)
                    gainEnv = 1.0f;          // snap → bypass path can go bit-exact

                minGain = juce::jmin (minGain, gainEnv);

                const float densTarget = raw < 0.999f ? 1.0f : 0.0f;
                densityEnv = flushDenormal (densTarget + densityCoef * (densityEnv - densTarget));

                // -- Lookahead delay; gain + matching trim on the delayed signal --
                int r = w - lookahead;
                if (r < 0) r += cap;

                const float g = gainEnv * trimG;
                for (int ch = 0; ch < channels; ++ch)
                {
                    ring[ch][w] = xd[ch];
                    io[ch][i] = ring[ch][r] * g;
                }

                if (++w >= cap) w = 0;
                ++n;
            }

            writePos = w;
            wedgeTime = n;
            maxGrDb.store (juce::jmax (0.0f, -gainToDb (minGain)), std::memory_order_relaxed);
        }

        /** Constant for a given prepare(); see latency contract above. */
        int latencySamples() const { return lookahead; }

        /** Worst-case limiter gain reduction of the last block, positive dB. */
        float getGainReductionDb() const { return maxGrDb.load (std::memory_order_relaxed); }

    private:
        float coefForMs (float ms) const
        {
            return (float) std::exp (-1.0 / (0.001 * (double) juce::jmax (0.01f, ms) * sampleRate));
        }

        static constexpr int maxChannels = 8;
        static constexpr float interSampleGuardDb = 0.3f;

        double sampleRate = 48000.0;
        int lookahead = 72;
        juce::AudioBuffer<float> delay;
        int writePos = 0;
        Params params;

        // Monotonic wedge (index/value pairs) over the lookahead window.
        int window = 73;
        std::vector<juce::int64> wedgeIdx;
        std::vector<float> wedgeVal;
        int wedgeHead = 0, wedgeCount = 0;
        juce::int64 wedgeTime = 0;

        float attackCoef = 0.9f, releaseCoef = 0.999f, densityCoef = 0.999f;
        float gainEnv = 1.0f;        // smoothed limiter gain, 1 = no reduction
        float densityEnv = 0.0f;     // how continuously the limiter is working
        float peak1 = 0.0f, peak2 = 0.0f;   // sidechain peak history (TP estimate)

        BallisticsFilter programmeEnv, fastEnv, slowEnv;

        juce::SmoothedValue<float> amountSm, driveSm, trimSm,
                                   thresholdSm, characterSm, preserveSm;

        std::atomic<float> maxGrDb { 0.0f };
    };
} // namespace sauce::dsp
