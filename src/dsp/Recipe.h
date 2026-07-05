#pragma once

#include "DspHelpers.h"
#include "AnalogDrift.h"
#include "SaturationSuite.h"
#include "MicroDynamics.h"
#include "MusicalEQ.h"
#include "StereoProcessor.h"
#include "LowEndTightener.h"
#include "LoudnessMaximizer.h"
#include "Detector.h"
#include "../Parameters.h"

namespace sauce::dsp
{
    // =========================================================================
    // Recipe — the point of view of the plugin.
    //
    // One place converts user-facing macros (Sauce, Heat, Glue, Warmth, …),
    // the sensed/forced programme type, the Vintage/Modern recipe switch and
    // the analog drift state into concrete settings for every processor.
    //
    // Two rules keep it honest:
    //   1. Sauce is a *baseline*: it raises many processors a little, on
    //      perceptual tapers, scaled per programme. Individual knobs then add
    //      on top — they always work, with or without Sauce.
    //   2. Everything at zero == silence from the recipe: all modules receive
    //      neutral settings and switch to true bypass (null test material).
    // =========================================================================

    /** Block-rate snapshot of every user parameter (already denormalised). */
    struct RawParams
    {
        float sauce = 35.0f;          // 0..100
        int   character = 1;          // 0 vintage, 1 modern
        int   console = 1;            // 0..6
        int   programMode = 0;        // sauce::param::Program

        float warmth = 0, air = 0, body = 0, presence = 0, weight = 0, sparkle = 0; // -100..100
        float glue = 0, punch = 0, density = 0, snap = 0;                            // 0..100
        float heat = 20.0f;           // 0..100
        float width = 100.0f;         // 0..200
        bool  monoSafe = true;
        float bassMonoHz = 120.0f;
        float tight = 0.0f;           // 0..100
        float loud = 0.0f;            // 0..100
        float ceilingDb = -1.0f;
        float driftDepth = 15.0f;     // 0..100
        float scHighpassHz = 20.0f;
        float scLowpassHz = 20000.0f;
        bool  extSidechain = false;
    };

    /** Everything the audio chain needs for one block. */
    struct RecipeSettings
    {
        SaturationSuite::Params   sat;
        MicroDynamics::Params     dyn;
        MusicalEQ::Params         eq;
        StereoProcessor::Params   stereo;
        LowEndTightener::Params   low;
        LoudnessMaximizer::Params max;
        Detector::Params          detector;
    };

    namespace detail
    {
        /** Per-programme scaling of the Sauce baseline. */
        struct ProgramScale
        {
            float sat, glue, punch, density, snap, width, tight, air, warmth, loud;
        };

        inline const ProgramScale& scaleFor (param::Program p)
        {
            using P = param::Program;
            //                                        sat  glue punch dens snap width tight air  warm loud
            static constexpr ProgramScale vocals   { 0.8f, 0.5f, 0.2f, 0.7f, 0.2f, 0.5f, 0.2f, 0.9f, 0.6f, 0.3f };
            static constexpr ProgramScale drums    { 1.0f, 0.9f, 1.0f, 0.4f, 0.9f, 0.6f, 1.0f, 0.4f, 0.3f, 0.5f };
            static constexpr ProgramScale bass     { 1.1f, 0.6f, 0.5f, 0.6f, 0.1f, 0.0f, 1.2f, 0.0f, 0.5f, 0.3f };
            static constexpr ProgramScale guitar   { 1.0f, 0.6f, 0.5f, 0.6f, 0.4f, 0.7f, 0.4f, 0.5f, 0.7f, 0.3f };
            static constexpr ProgramScale keys     { 0.7f, 0.5f, 0.4f, 0.6f, 0.4f, 0.8f, 0.3f, 0.7f, 0.6f, 0.3f };
            static constexpr ProgramScale synth    { 0.9f, 0.5f, 0.4f, 0.5f, 0.5f, 1.0f, 0.5f, 0.6f, 0.4f, 0.4f };
            static constexpr ProgramScale mixBus   { 0.7f, 1.0f, 0.5f, 0.5f, 0.4f, 0.7f, 0.7f, 0.6f, 0.4f, 0.5f };
            static constexpr ProgramScale master   { 0.5f, 0.7f, 0.3f, 0.4f, 0.3f, 0.4f, 0.5f, 0.5f, 0.3f, 0.8f };

            switch (p)
            {
                case P::vocals: return vocals;
                case P::drums:  return drums;
                case P::bass:   return bass;
                case P::guitar: return guitar;
                case P::keys:   return keys;
                case P::synth:  return synth;
                case P::master: return master;
                case P::autoDetect:
                case P::mixBus:
                default:        return mixBus;
            }
        }

        /** Console model harmonic recipes: stage weights + intensity + tilt. */
        struct ConsoleFlavour { float tape, tube, transformer, tilt, intensity; };

        inline const ConsoleFlavour& consoleFlavour (int model)
        {
            //                                            tape  tube  xfmr  tilt   amt
            static constexpr ConsoleFlavour off       { 0.0f, 0.0f, 0.0f,  0.0f, 0.0f };
            static constexpr ConsoleFlavour gold      { 0.3f, 0.5f, 0.4f, -0.1f, 0.7f }; // warm British desk
            static constexpr ConsoleFlavour silver    { 0.1f, 0.3f, 0.2f,  0.2f, 0.5f }; // clean, airy
            static constexpr ConsoleFlavour crimson   { 0.5f, 0.6f, 0.3f, -0.2f, 0.9f }; // hot, mid-forward
            static constexpr ConsoleFlavour emerald   { 0.4f, 0.2f, 0.5f, -0.05f, 0.6f }; // thick lows
            static constexpr ConsoleFlavour titanium  { 0.05f, 0.15f, 0.1f, 0.1f, 0.35f }; // near-clinical sheen
            static constexpr ConsoleFlavour copper    { 0.45f, 0.35f, 0.6f, -0.15f, 0.8f }; // transformer-heavy

            switch (model)
            {
                case 1: return gold;
                case 2: return silver;
                case 3: return crimson;
                case 4: return emerald;
                case 5: return titanium;
                case 6: return copper;
                default: return off;
            }
        }
    }

    class Recipe
    {
    public:
        void setDrift (const AnalogDrift* d) { drift = d; }

        RecipeSettings compute (const RawParams& p, param::Program sensedProgram) const
        {
            RecipeSettings s;

            const auto program = p.programMode == 0 ? sensedProgram
                                                    : (param::Program) p.programMode;
            const auto& ps = detail::scaleFor (program);

            const float s01       = macroTaper (pct (p.sauce)); // perceptual Sauce baseline
            const float character = p.character == 0 ? 0.0f : 1.0f;
            const float dDepth    = pct (p.driftDepth);

            // ---------------- Saturation -----------------------------------
            {
                const auto& con = detail::consoleFlavour (p.console);
                const float heat = pct (p.heat);
                // Sauce baseline + Heat on top; programme scales the baseline.
                const float colour = clamp01 (0.45f * s01 * ps.sat + heat * (0.35f + 0.65f * s01));

                s.sat.driveDb     = 16.0f * colour;
                s.sat.tape        = clamp01 (colour * (0.5f + 0.5f * (1.0f - character)) + con.tape * con.intensity * 0.3f);
                s.sat.tube        = clamp01 (colour * 0.6f + con.tube * con.intensity * 0.3f);
                s.sat.transformer = clamp01 (colour * 0.35f * (1.0f - 0.4f * character) + con.transformer * con.intensity * 0.3f);
                s.sat.clip        = clamp01 (colour * 0.5f * character);
                s.sat.dynHarm     = clamp01 (0.6f * s01 * ps.sat);
                s.sat.console     = p.console;
                s.sat.consoleAmt  = con.intensity * (0.35f + 0.65f * clamp01 (colour + 0.3f * s01));
                s.sat.character   = character;

                if (drift != nullptr)
                {
                    s.sat.chanDriveMul[0] = drift->driveMul (0, dDepth);
                    s.sat.chanDriveMul[1] = drift->driveMul (1, dDepth);
                }

                s.sat.active = colour > 1.0e-4f || (p.console > 0 && s.sat.consoleAmt > 1.0e-4f);
            }

            // ---------------- Micro dynamics --------------------------------
            {
                s.dyn.glue    = clamp01 (0.55f * s01 * ps.glue    + pct (p.glue));
                s.dyn.punch   = clamp01 (0.35f * s01 * ps.punch   + pct (p.punch));
                s.dyn.density = clamp01 (0.30f * s01 * ps.density + pct (p.density));
                s.dyn.snap    = clamp01 (0.25f * s01 * ps.snap    + pct (p.snap));
                s.dyn.character = character;

                if (drift != nullptr)
                {
                    s.dyn.chanTimeMul[0] = drift->timeMul (0, dDepth);
                    s.dyn.chanTimeMul[1] = drift->timeMul (1, dDepth);
                }

                s.dyn.active = (s.dyn.glue + s.dyn.punch + s.dyn.density + s.dyn.snap) > 1.0e-4f;
            }

            // ---------------- Musical EQ ------------------------------------
            {
                // Sauce adds a whisper of air + warmth (the "expensive" tilt),
                // programme-weighted; user knobs are the main voice.
                s.eq.warmth   = juce::jlimit (-1.0f, 1.0f, bipolar (p.warmth)   + 0.10f * s01 * ps.warmth);
                s.eq.air      = juce::jlimit (-1.0f, 1.0f, bipolar (p.air)      + 0.15f * s01 * ps.air);
                s.eq.body     = juce::jlimit (-1.0f, 1.0f, bipolar (p.body));
                s.eq.presence = juce::jlimit (-1.0f, 1.0f, bipolar (p.presence) + 0.05f * s01);
                s.eq.weight   = juce::jlimit (-1.0f, 1.0f, bipolar (p.weight));
                s.eq.sparkle  = juce::jlimit (-1.0f, 1.0f, bipolar (p.sparkle));
                s.eq.character = character;

                if (drift != nullptr)
                {
                    s.eq.chanFreqMul[0] = drift->freqMul (0, dDepth);
                    s.eq.chanFreqMul[1] = drift->freqMul (1, dDepth);
                }

                const float total = std::abs (s.eq.warmth) + std::abs (s.eq.air) + std::abs (s.eq.body)
                                  + std::abs (s.eq.presence) + std::abs (s.eq.weight) + std::abs (s.eq.sparkle);
                s.eq.active = total > 1.0e-4f;
            }

            // ---------------- Stereo ----------------------------------------
            {
                const float userWidth = p.width * 0.01f;               // 0..2
                const float sauceWiden = 0.18f * s01 * ps.width;        // subtle
                s.stereo.width = juce::jlimit (0.0f, 2.0f, userWidth + sauceWiden);
                s.stereo.sideSheen = clamp01 (0.5f * s01 * ps.air);
                s.stereo.monoSafe = p.monoSafe;
                s.stereo.bassMonoHz = p.bassMonoHz;
                s.stereo.character = character;
                s.stereo.active = std::abs (s.stereo.width - 1.0f) > 1.0e-4f
                                   || s.stereo.sideSheen > 1.0e-4f
                                   || p.bassMonoHz > 0.5f;
            }

            // ---------------- Low end ---------------------------------------
            {
                s.low.tight    = clamp01 (0.35f * s01 * ps.tight + pct (p.tight));
                s.low.bassHarm = clamp01 (0.6f * s.low.tight);
                s.low.character = character;

                if (drift != nullptr)
                {
                    s.low.chanFreqMul[0] = drift->freqMul (0, dDepth * 0.5f);
                    s.low.chanFreqMul[1] = drift->freqMul (1, dDepth * 0.5f);
                }

                s.low.active = s.low.tight > 1.0e-4f;
            }

            // ---------------- Loudness --------------------------------------
            {
                s.max.amount = clamp01 (0.25f * s01 * ps.loud + pct (p.loud));
                s.max.ceilingDb = p.ceilingDb;
                s.max.transientPreserve = 0.5f + 0.4f * (1.0f - character) + 0.1f;
                s.max.character = character;
                s.max.active = s.max.amount > 1.0e-4f;
            }

            // ---------------- Detector --------------------------------------
            s.detector.highpassHz  = p.scHighpassHz;
            s.detector.lowpassHz   = p.scLowpassHz;
            s.detector.useExternal = p.extSidechain;

            return s;
        }

    private:
        const AnalogDrift* drift = nullptr;
    };
} // namespace sauce::dsp
