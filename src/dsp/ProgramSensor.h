#pragma once

#include "DspHelpers.h"
#include "../Parameters.h"

#include <atomic>

namespace sauce::dsp
{
    // =========================================================================
    // ProgramSensor — source detection ("AI Mix Detection", honestly labelled:
    // a transparent feature heuristic, not a network).
    //
    // Accumulates cheap per-sample features from the *input* signal into
    // ~250 ms frames (no FFT, just one-poles and accumulators):
    //   - crest factor (frame peak over frame RMS)
    //   - spectral balance: four one-pole splits at ~150 Hz / ~600 Hz /
    //     ~2.5 kHz / ~6 kHz -> low / lowmid / highmid / high energy ratios
    //     (plus an "air" >6 kHz sub-ratio for cymbal/brightness rules)
    //   - onset rate: fast envelope jumping ~6 dB over a slow one, per second
    //   - stereo: side/mid energy ratio (0 for mono input)
    //   - sustain: mean/peak of the fast envelope (decaying hits vs pads)
    //   - frame-to-frame band-ratio flux (spectral stability)
    //
    // Classifies into sauce::param::Program (vocals/drums/bass/guitar/keys/
    // synth/mixBus/master) with:
    //   - a fuzzy score table per programme; winner = argmax, confidence =
    //     margin over the runner-up mapped to 0..1
    //   - hysteresis: only re-classify after a challenger wins >= 2 s of
    //     consecutive non-silent frames at confidence >= 0.45, so the recipe
    //     never flip-flops mid-phrase
    //   - below ~0.4 confidence, frames vote for mixBus (the safe default);
    //     silence (< -60 dBFS RMS) freezes the current decision entirely
    //
    // Requirements honoured:
    //   - analyze() is cheap (a few one-poles + accumulators) and allocation/
    //     lock/exception free; classification maths runs once per frame.
    //   - current()/confidence() are safe from the editor thread (relaxed
    //     atomics) and stable between hysteresis flips. Starts at mixBus.
    // =========================================================================
    class ProgramSensor
    {
    public:
        void prepare (double newSampleRate, int maxBlockSize, int numChannels)
        {
            sampleRate = newSampleRate;
            juce::ignoreUnused (maxBlockSize, numChannels);

            frameLen     = juce::jmax (32, (int) std::lround (sampleRate * frameSeconds));
            invFrameLen  = 1.0f / (float) frameLen;
            frameSecReal = (float) ((double) frameLen / sampleRate);

            lp150.prepare  (sampleRate, 150.0f);
            lp600.prepare  (sampleRate, 600.0f);
            lp2500.prepare (sampleRate, 2500.0f);
            lp6000.prepare (sampleRate, 6000.0f);

            fastEnv.prepare (sampleRate, 1.0f, 60.0f);    // rides individual hits
            slowEnv.prepare (sampleRate, 80.0f, 300.0f);  // tracks programme level

            resetState();
        }

        void reset() { resetState(); }

        void analyze (const juce::AudioBuffer<float>& input, int numSamples)
        {
            const int channels = juce::jmin (input.getNumChannels(), 2);
            if (channels <= 0 || numSamples <= 0)
                return;

            const float* left  = input.getReadPointer (0);
            const float* right = channels > 1 ? input.getReadPointer (1) : nullptr;

            for (int i = 0; i < numSamples; ++i)
            {
                const float l    = left[i];
                const float r    = right != nullptr ? right[i] : l;
                const float mid  = 0.5f * (l + r);
                const float side = right != nullptr ? 0.5f * (l - r) : 0.0f;

                // Four cumulative lowpasses -> band signals by subtraction.
                const float a150 = lp150.processSample (mid);
                const float a600 = lp600.processSample (mid);
                const float a2k5 = lp2500.processSample (mid);
                const float a6k  = lp6000.processSample (mid);

                const float bLow     = a150;        // < 150 Hz
                const float bLowMid  = a600 - a150; // 150–600 Hz
                const float bHighMid = a2k5 - a600; // 600 Hz–2.5 kHz
                const float bHigh    = mid - a2k5;  // > 2.5 kHz
                const float bAir     = mid - a6k;   // > 6 kHz (subset of high)

                eLow     += (double) (bLow * bLow);
                eLowMid  += (double) (bLowMid * bLowMid);
                eHighMid += (double) (bHighMid * bHighMid);
                eHigh    += (double) (bHigh * bHigh);
                eAir     += (double) (bAir * bAir);

                sumSqMid  += (double) (mid * mid);
                sumSqSide += (double) (side * side);

                const float mag = std::abs (mid);
                peakAbs = juce::jmax (peakAbs, mag);

                const float fe = fastEnv.processSample (mag);
                const float se = slowEnv.processSample (mag);
                envSum += (double) fe;
                envPeak = juce::jmax (envPeak, fe);

                // Onset: fast envelope jumps ~6 dB over the slow one (and is
                // above -60 dBFS); re-arm once it falls back within ~2 dB.
                if (onsetArmed)
                {
                    if (fe > 2.0f * se && fe > silenceRmsLin)
                    {
                        ++onsetCount;
                        onsetArmed = false;
                    }
                }
                else if (fe < 1.26f * se + 1.0e-6f)
                {
                    onsetArmed = true;
                }

                if (++framePos >= frameLen)
                    finalizeFrame();
            }
        }

        /** Stable detected program (hysteresis applied). */
        sauce::param::Program current() const
        {
            return (sauce::param::Program) detectedShared.load (std::memory_order_relaxed);
        }

        /** 0..1 confidence in the current classification. */
        float confidence() const { return confShared.load (std::memory_order_relaxed); }

    private:
        using Program = sauce::param::Program;

        //==== primitives ======================================================
        /** One-pole lowpass, denormal-flushed (safe recursive state). */
        struct OnePoleLP
        {
            void prepare (double sr, float hz)
            {
                k = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * hz / sr);
                z = 0.0f;
            }

            float processSample (float x)
            {
                z = flushDenormal (z + k * (x - z));
                return z;
            }

            void reset() { z = 0.0f; }

            float k = 0.0f, z = 0.0f;
        };

        /** One frame's worth of features, all cheap ratios/rates. */
        struct Features
        {
            float crestDb;                        // frame peak over RMS, dB
            float rLow, rLowMid, rHighMid, rHigh; // band energy ratios (sum ~1)
            float rAir;                           // > 6 kHz share (inside rHigh)
            float onsetsPerSec;                   // envelope jumps per second
            float sideRatio;                      // side / (mid + side) energy
            float sustain;                        // env mean/peak: 0 spiky, 1 held
            float flux;                           // smoothed band-ratio movement
        };

        // Soft memberships: 0..1 ramps of 'width' centred on the edge.
        static float above  (float x, float edge, float width) { return clamp01 ((x - edge) / width + 0.5f); }
        static float below  (float x, float edge, float width) { return 1.0f - above (x, edge, width); }
        static float within (float x, float lo, float hi, float width) { return above (x, lo, width) * below (x, hi, width); }

        //==== score table (each 0..1, average of its rules) ===================
        static float scoreBass (const Features& f)
        {
            float s = 0.0f;
            s += above (f.rLow, 0.45f, 0.20f);               // dominant low band
            s += below (f.rHighMid + f.rHigh, 0.22f, 0.15f); // narrowband: little above 600 Hz
            s += below (f.onsetsPerSec, 3.0f, 2.0f);         // notes, not hits
            s += below (f.sideRatio, 0.12f, 0.10f);          // bass lives in the centre
            s += above (f.sustain, 0.40f, 0.20f);            // notes ring between attacks
            return s * (1.0f / 5.0f);
        }

        static float scoreDrums (const Features& f)
        {
            float s = 0.0f;
            s += above (f.crestDb, 12.0f, 4.0f);     // hits: high peak over RMS
            s += above (f.onsetsPerSec, 4.0f, 3.0f); // dense transient stream
            s += above (f.rLow, 0.12f, 0.10f);       // kick / floor-tom low end
            s += above (f.rAir, 0.06f, 0.05f);       // cymbal / snare sizzle above 6 kHz
            s += below (f.sustain, 0.40f, 0.15f);    // envelope decays between hits
            return s * (1.0f / 5.0f);
        }

        static float scoreVocals (const Features& f)
        {
            float s = 0.0f;
            s += above (f.rHighMid, 0.32f, 0.15f);          // 600 Hz–2.5 kHz forward
            s += below (f.rLow, 0.20f, 0.12f);              // little sub content
            s += within (f.onsetsPerSec, 0.5f, 4.5f, 1.5f); // syllabic onset rate
            s += below (f.sideRatio, 0.12f, 0.08f);         // centred, mono-ish
            s += within (f.sustain, 0.30f, 0.80f, 0.15f);   // sustained with pauses
            return s * (1.0f / 5.0f);
        }

        static float scoreGuitar (const Features& f)
        {
            float s = 0.0f;
            s += above (f.rLowMid + f.rHighMid, 0.55f, 0.15f); // body + bite: mid-forward
            s += within (f.onsetsPerSec, 1.0f, 6.0f, 1.5f);    // strums / picking
            s += above (f.sustain, 0.45f, 0.15f);              // chords ring
            s += below (f.rLow, 0.30f, 0.15f);                 // little true sub
            return s * (1.0f / 4.0f);
        }

        static float scoreKeys (const Features& f)
        {
            float s = 0.0f;
            s += within (f.rLowMid, 0.15f, 0.50f, 0.10f);  // balanced low mids
            s += within (f.rHighMid, 0.15f, 0.50f, 0.10f); // balanced high mids
            s += below (f.onsetsPerSec, 3.5f, 1.5f);       // soft note attacks
            s += above (f.sustain, 0.50f, 0.15f);          // held chords
            s += above (f.sideRatio, 0.10f, 0.08f);        // stereo-miked / spread
            return s * (1.0f / 5.0f);
        }

        static float scoreSynth (const Features& f)
        {
            float s = 0.0f;
            s += above (f.sideRatio, 0.20f, 0.10f); // wide stereo image
            s += above (f.sustain, 0.60f, 0.15f);   // pads / held notes
            s += above (f.rHigh, 0.18f, 0.10f);     // bright, broadband top
            s += below (f.flux, 0.25f, 0.15f);      // static spectral balance
            return s * (1.0f / 4.0f);
        }

        static float scoreMixBus (const Features& f)
        {
            const float minBand = juce::jmin (juce::jmin (f.rLow, f.rLowMid),
                                              juce::jmin (f.rHighMid, f.rHigh));
            float s = 0.0f;
            s += above (minBand, 0.06f, 0.04f);             // every band populated
            s += within (f.crestDb, 8.0f, 14.0f, 3.0f);     // typical unmastered bus crest
            s += above (f.sustain, 0.50f, 0.15f);           // continuous programme energy
            s += within (f.onsetsPerSec, 1.0f, 9.0f, 2.0f); // arrangement keeps moving
            return s * (1.0f / 4.0f) + 0.05f;               // safe-default prior when torn
        }

        static float scoreMaster (const Features& f)
        {
            const float minBand = juce::jmin (juce::jmin (f.rLow, f.rLowMid),
                                              juce::jmin (f.rHighMid, f.rHigh));
            float s = 0.0f;
            s += above (minBand, 0.06f, 0.04f);   // full-range programme
            s += below (f.crestDb, 8.5f, 2.0f);   // already-limited: low crest
            s += above (f.sustain, 0.65f, 0.12f); // dense, continuous
            s += below (f.flux, 0.30f, 0.15f);    // controlled, stable balance
            return s * (1.0f / 4.0f);
        }

        //==== frame machinery =================================================
        void finalizeFrame()
        {
            const float rms = std::sqrt ((float) sumSqMid * invFrameLen);

            if (rms < silenceRmsLin) // < -60 dBFS: freeze decision & confidence
            {
                resetFrame();
                return;
            }

            Features f;
            f.crestDb = gainToDb (peakAbs) - gainToDb (rms);

            const double total = eLow + eLowMid + eHighMid + eHigh + 1.0e-20;
            f.rLow     = (float) (eLow / total);
            f.rLowMid  = (float) (eLowMid / total);
            f.rHighMid = (float) (eHighMid / total);
            f.rHigh    = (float) (eHigh / total);
            f.rAir     = (float) (eAir / total);

            f.sideRatio    = (float) (sumSqSide / (sumSqMid + sumSqSide + 1.0e-20));
            f.onsetsPerSec = (float) onsetCount / frameSecReal;
            f.sustain      = clamp01 (((float) envSum * invFrameLen) / (envPeak + 1.0e-9f));

            // Frame-to-frame band-ratio flux (stability of the balance).
            if (hasPrevRatios)
            {
                const float d = std::abs (f.rLow - prevLow) + std::abs (f.rLowMid - prevLowMid)
                              + std::abs (f.rHighMid - prevHighMid) + std::abs (f.rHigh - prevHigh);
                fluxAvg += 0.25f * (d - fluxAvg);
            }
            prevLow = f.rLow; prevLowMid = f.rLowMid; prevHighMid = f.rHighMid; prevHigh = f.rHigh;
            hasPrevRatios = true;
            f.flux = fluxAvg;

            classify (f);
            resetFrame();
        }

        void classify (const Features& f)
        {
            const float scores[8] =
            {
                scoreVocals (f), scoreDrums (f), scoreBass (f), scoreGuitar (f),
                scoreKeys (f),   scoreSynth (f), scoreMixBus (f), scoreMaster (f)
            };
            static constexpr Program programs[8] =
            {
                Program::vocals, Program::drums, Program::bass, Program::guitar,
                Program::keys,   Program::synth, Program::mixBus, Program::master
            };

            int best = 0, second = 1;
            if (scores[1] > scores[0]) { best = 1; second = 0; }
            for (int i = 2; i < 8; ++i)
            {
                if (scores[i] > scores[best])        { second = best; best = i; }
                else if (scores[i] > scores[second]) { second = i; }
            }

            // Confidence = margin over the runner-up; a 0.25 score margin = 1.
            const float frameConf = clamp01 ((scores[best] - scores[second]) * marginToConf);

            // Vote: unsure frames back the safe default instead of a shaky winner.
            Program vote = programs[best];
            bool backsChallenger = frameConf >= flipConf;
            if (frameConf < fallbackConf)
            {
                vote = Program::mixBus;
                backsChallenger = true; // sustained ambiguity drifts back to mixBus
            }

            // Hysteresis: >= 2 s of consecutive supporting non-silent frames.
            if (vote == detectedLocal || ! backsChallenger)
            {
                challengerStreak = 0;
            }
            else if (vote == challenger)
            {
                ++challengerStreak;
            }
            else
            {
                challenger = vote;
                challengerStreak = 1;
            }

            if (challengerStreak >= framesToFlip)
            {
                detectedLocal = challenger;
                detectedShared.store ((int) detectedLocal, std::memory_order_relaxed);
                challengerStreak = 0;
                confSmooth = frameConf; // snap on flip so the meter follows
            }

            // Published confidence: current winner's margin over its best rival.
            float detScore = 0.0f, bestOther = 0.0f;
            for (int i = 0; i < 8; ++i)
            {
                if (programs[i] == detectedLocal) detScore  = scores[i];
                else                              bestOther = juce::jmax (bestOther, scores[i]);
            }
            const float confRaw = clamp01 ((detScore - bestOther) * marginToConf);
            confSmooth += 0.35f * (confRaw - confSmooth);
            confShared.store (confSmooth, std::memory_order_relaxed);
        }

        void resetFrame()
        {
            sumSqMid = sumSqSide = 0.0;
            eLow = eLowMid = eHighMid = eHigh = eAir = 0.0;
            envSum = 0.0;
            peakAbs = envPeak = 0.0f;
            framePos = onsetCount = 0;
        }

        void resetState()
        {
            lp150.reset(); lp600.reset(); lp2500.reset(); lp6000.reset();
            fastEnv.reset();
            slowEnv.reset();

            resetFrame();
            onsetArmed = true;
            hasPrevRatios = false;
            prevLow = prevLowMid = prevHighMid = prevHigh = 0.0f;
            fluxAvg = 0.0f;

            detectedLocal = Program::mixBus;
            challenger = Program::mixBus;
            challengerStreak = 0;
            confSmooth = 0.0f;

            detectedShared.store ((int) Program::mixBus, std::memory_order_relaxed);
            confShared.store (0.0f, std::memory_order_relaxed);
        }

        //==== tuning ==========================================================
        static constexpr float frameSeconds  = 0.25f;   // feature frame length
        static constexpr int   framesToFlip  = 8;       // 8 x 250 ms = 2 s hysteresis
        static constexpr float silenceRmsLin = 1.0e-3f; // -60 dBFS freeze gate
        static constexpr float marginToConf  = 4.0f;    // 0.25 score margin -> conf 1
        static constexpr float flipConf      = 0.45f;   // challenger must be this sure
        static constexpr float fallbackConf  = 0.40f;   // below this, vote mixBus

        //==== state ===========================================================
        double sampleRate = 48000.0;
        int    frameLen = 12000;
        float  invFrameLen = 1.0f / 12000.0f;
        float  frameSecReal = 0.25f;

        OnePoleLP lp150, lp600, lp2500, lp6000;
        BallisticsFilter fastEnv, slowEnv;

        // Frame accumulators (all reset every ~250 ms).
        double sumSqMid = 0.0, sumSqSide = 0.0;
        double eLow = 0.0, eLowMid = 0.0, eHighMid = 0.0, eHigh = 0.0, eAir = 0.0;
        double envSum = 0.0;
        float  peakAbs = 0.0f, envPeak = 0.0f;
        int    framePos = 0, onsetCount = 0;
        bool   onsetArmed = true;

        // Frame-to-frame stability.
        float prevLow = 0.0f, prevLowMid = 0.0f, prevHighMid = 0.0f, prevHigh = 0.0f;
        bool  hasPrevRatios = false;
        float fluxAvg = 0.0f;

        // Hysteresis (audio-thread only).
        Program detectedLocal = Program::mixBus;
        Program challenger = Program::mixBus;
        int     challengerStreak = 0;
        float   confSmooth = 0.0f;

        // Editor-visible state (relaxed atomics).
        std::atomic<int>   detectedShared { (int) Program::mixBus };
        std::atomic<float> confShared { 0.0f };
    };
} // namespace sauce::dsp
