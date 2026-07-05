#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// =============================================================================
// The Secret Sauce — parameter contract.
//
// Every parameter ID, range, default and skew lives here so the processor,
// editor, presets, snapshots and tests all agree. IDs are versioned via
// juce::ParameterID so future changes can migrate cleanly.
// =============================================================================

namespace sauce::param
{
    // --- IDs -----------------------------------------------------------------
    inline constexpr const char* sauceAmt     = "sauce";        // the big knob
    inline constexpr const char* character    = "character";    // Vintage / Modern
    inline constexpr const char* console      = "console";      // colour model
    inline constexpr const char* programMode  = "programMode";  // Auto / forced source type

    // Frequency Magic (all bipolar, -100..100)
    inline constexpr const char* warmth       = "warmth";
    inline constexpr const char* air          = "air";
    inline constexpr const char* body         = "body";
    inline constexpr const char* presence     = "presence";
    inline constexpr const char* weight       = "weight";
    inline constexpr const char* sparkle      = "sparkle";

    // Micro dynamics (0..100)
    inline constexpr const char* glue         = "glue";
    inline constexpr const char* punch        = "punch";
    inline constexpr const char* density      = "density";
    inline constexpr const char* snap         = "snap";

    // Colour / saturation macro (0..100)
    inline constexpr const char* heat         = "heat";

    // Stereo
    inline constexpr const char* width        = "width";        // 0..200 %
    inline constexpr const char* monoSafe     = "monoSafe";     // bool
    inline constexpr const char* bassMonoHz   = "bassMonoHz";   // 0 (off) .. 300 Hz

    // Low end
    inline constexpr const char* tight        = "tight";        // 0..100

    // Loudness
    inline constexpr const char* loud         = "loud";         // 0..100
    inline constexpr const char* ceiling      = "ceiling";      // -3..0 dBTP

    // I/O
    inline constexpr const char* inTrim       = "inTrim";       // -24..24 dB
    inline constexpr const char* output       = "output";       // -24..24 dB
    inline constexpr const char* mix          = "mix";          // 0..100 %
    inline constexpr const char* autoGain     = "autoGain";     // bool
    inline constexpr const char* delta        = "delta";        // bool
    inline constexpr const char* bypass       = "bypass";       // bool (host automatable)

    // Quality
    inline constexpr const char* oversampling = "os";           // Auto/Off/2x/4x/8x/16x
    inline constexpr const char* linearPhase  = "linPhase";     // bool (EQ)

    // Analog drift (component variation depth, 0..100)
    inline constexpr const char* drift        = "drift";

    // Sidechain
    inline constexpr const char* extSidechain = "extSC";        // bool
    inline constexpr const char* scHighpass   = "scHP";         // 20..500 Hz
    inline constexpr const char* scLowpass    = "scLP";         // 1k..20k Hz

    // --- Choice lists ----------------------------------------------------------
    inline const juce::StringArray characterChoices { "Vintage", "Modern" };
    inline const juce::StringArray consoleChoices   { "Off", "Gold", "Silver", "Crimson",
                                                      "Emerald", "Titanium", "Copper" };
    inline const juce::StringArray programChoices   { "Auto", "Vocals", "Drums", "Bass",
                                                      "Guitar", "Keys", "Synth",
                                                      "Mix Bus", "Master" };
    inline const juce::StringArray osChoices        { "Auto", "Off", "2x", "4x", "8x", "16x" };

    // Program indices (match programChoices)
    enum class Program { autoDetect = 0, vocals, drums, bass, guitar, keys, synth, mixBus, master };

    // --- Layout ----------------------------------------------------------------
    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        using P  = AudioParameterFloat;
        using PB = AudioParameterBool;
        using PC = AudioParameterChoice;

        auto pct     = [] { return NormalisableRange<float> (0.0f, 100.0f, 0.01f); };
        auto bipolar = [] { return NormalisableRange<float> (-100.0f, 100.0f, 0.01f); };
        auto dB24    = [] { return NormalisableRange<float> (-24.0f, 24.0f, 0.01f); };

        auto pctLabel = AudioParameterFloatAttributes().withLabel ("%");
        auto dbLabel  = AudioParameterFloatAttributes().withLabel ("dB");
        auto hzLabel  = AudioParameterFloatAttributes().withLabel ("Hz");

        std::vector<std::unique_ptr<RangedAudioParameter>> params;

        params.push_back (std::make_unique<P>  (ParameterID { sauceAmt, 1 },  "Sauce",     pct(), 35.0f, pctLabel));
        params.push_back (std::make_unique<PC> (ParameterID { character, 1 }, "Recipe",    characterChoices, 1));
        params.push_back (std::make_unique<PC> (ParameterID { console, 1 },   "Console",   consoleChoices, 1));
        params.push_back (std::make_unique<PC> (ParameterID { programMode, 1 },"Program",  programChoices, 0));

        params.push_back (std::make_unique<P> (ParameterID { warmth, 1 },   "Warmth",   bipolar(), 0.0f, pctLabel));
        params.push_back (std::make_unique<P> (ParameterID { air, 1 },      "Air",      bipolar(), 0.0f, pctLabel));
        params.push_back (std::make_unique<P> (ParameterID { body, 1 },     "Body",     bipolar(), 0.0f, pctLabel));
        params.push_back (std::make_unique<P> (ParameterID { presence, 1 }, "Presence", bipolar(), 0.0f, pctLabel));
        params.push_back (std::make_unique<P> (ParameterID { weight, 1 },   "Weight",   bipolar(), 0.0f, pctLabel));
        params.push_back (std::make_unique<P> (ParameterID { sparkle, 1 },  "Sparkle",  bipolar(), 0.0f, pctLabel));

        params.push_back (std::make_unique<P> (ParameterID { glue, 1 },    "Glue",    pct(), 0.0f, pctLabel));
        params.push_back (std::make_unique<P> (ParameterID { punch, 1 },   "Punch",   pct(), 0.0f, pctLabel));
        params.push_back (std::make_unique<P> (ParameterID { density, 1 }, "Density", pct(), 0.0f, pctLabel));
        params.push_back (std::make_unique<P> (ParameterID { snap, 1 },    "Snap",    pct(), 0.0f, pctLabel));

        params.push_back (std::make_unique<P> (ParameterID { heat, 1 },    "Heat",    pct(), 20.0f, pctLabel));

        params.push_back (std::make_unique<P>  (ParameterID { width, 1 },    "Width",
                              NormalisableRange<float> (0.0f, 200.0f, 0.01f), 100.0f, pctLabel));
        params.push_back (std::make_unique<PB> (ParameterID { monoSafe, 1 }, "Mono Safe", true));
        params.push_back (std::make_unique<P>  (ParameterID { bassMonoHz, 1 }, "Bass Mono",
                              NormalisableRange<float> (0.0f, 300.0f, 1.0f), 120.0f, hzLabel));

        params.push_back (std::make_unique<P> (ParameterID { tight, 1 }, "Tight", pct(), 0.0f, pctLabel));

        params.push_back (std::make_unique<P> (ParameterID { loud, 1 },    "Wow",    pct(), 0.0f, pctLabel));
        params.push_back (std::make_unique<P> (ParameterID { ceiling, 1 }, "Ceiling",
                              NormalisableRange<float> (-3.0f, 0.0f, 0.01f), -1.0f, dbLabel));

        params.push_back (std::make_unique<P>  (ParameterID { inTrim, 1 },  "Input Trim", dB24(), 0.0f, dbLabel));
        params.push_back (std::make_unique<P>  (ParameterID { output, 1 },  "Output",     dB24(), 0.0f, dbLabel));
        params.push_back (std::make_unique<P>  (ParameterID { mix, 1 },     "Mix",        pct(), 100.0f, pctLabel));
        params.push_back (std::make_unique<PB> (ParameterID { autoGain, 1 },"Auto Gain",  true));
        params.push_back (std::make_unique<PB> (ParameterID { delta, 1 },   "Delta",      false));
        params.push_back (std::make_unique<PB> (ParameterID { bypass, 1 },  "Bypass",     false,
                              AudioParameterBoolAttributes().withLabel ("Bypass")));

        params.push_back (std::make_unique<PC> (ParameterID { oversampling, 1 }, "Oversampling", osChoices, 0));
        params.push_back (std::make_unique<PB> (ParameterID { linearPhase, 1 },  "Linear Phase", false));

        params.push_back (std::make_unique<P> (ParameterID { drift, 1 }, "Drift", pct(), 15.0f, pctLabel));

        params.push_back (std::make_unique<PB> (ParameterID { extSidechain, 1 }, "Ext Sidechain", false));
        params.push_back (std::make_unique<P>  (ParameterID { scHighpass, 1 },   "SC High Pass",
                              NormalisableRange<float> (20.0f, 500.0f, 1.0f, 0.4f), 20.0f, hzLabel));
        params.push_back (std::make_unique<P>  (ParameterID { scLowpass, 1 },    "SC Low Pass",
                              NormalisableRange<float> (1000.0f, 20000.0f, 1.0f, 0.4f), 20000.0f, hzLabel));

        return { params.begin(), params.end() };
    }
} // namespace sauce::param
