#pragma once

#include "../Parameters.h"

namespace sauce::state
{
    // =========================================================================
    // Factory presets. Deliberately curated starting points that teach the
    // plugin: each shows one intended use, names say where to put it, and
    // nothing is extreme. Values are in *denormalised* parameter units.
    // =========================================================================
    struct Preset
    {
        const char* name;
        const char* category;
        std::vector<std::pair<const char*, float>> values;
    };

    inline const std::vector<Preset>& factoryPresets()
    {
        namespace p = sauce::param;

        static const std::vector<Preset> presets {
            // Teaches: the factory state itself — load this to hear what "neutral" is.
            { "Default", "Init", {} },

            // Teaches: the one-knob promise — Sauce alone, everything else at default.
            { "Secret Ingredient", "Anywhere", {
                { p::sauceAmt, 65.0f } } },

            // Teaches: Vintage + Silver for smooth top; Density keeps a vocal seated.
            { "Vocal Silk", "Vocals", {
                { p::sauceAmt, 45.0f }, { p::programMode, 1.0f }, { p::character, 0.0f },
                { p::console, 2.0f }, { p::heat, 25.0f }, { p::air, 25.0f },
                { p::presence, 15.0f }, { p::density, 30.0f }, { p::width, 95.0f } } },

            // Teaches: Modern + Air/Sparkle brighten without bite; Density does the levelling.
            { "Lead Vocal Bright", "Vocals", {
                { p::sauceAmt, 40.0f }, { p::programMode, 1.0f }, { p::console, 2.0f },
                { p::heat, 15.0f }, { p::air, 30.0f }, { p::presence, 20.0f },
                { p::sparkle, 10.0f }, { p::density, 25.0f } } },

            // Teaches: Density is the "every word up front" control; Crimson adds mid-forward bite.
            { "Rap Vocal Upfront", "Vocals", {
                { p::sauceAmt, 45.0f }, { p::programMode, 1.0f }, { p::console, 3.0f },
                { p::heat, 25.0f }, { p::density, 45.0f }, { p::punch, 10.0f },
                { p::presence, 18.0f }, { p::width, 90.0f } } },

            // Teaches: Glue + Punch + Snap shape a kit three ways; Tight firms the kick.
            { "Drum Bus Glue", "Drums", {
                { p::sauceAmt, 50.0f }, { p::programMode, 2.0f }, { p::character, 0.0f },
                { p::console, 3.0f }, { p::glue, 40.0f }, { p::punch, 35.0f },
                { p::snap, 25.0f }, { p::heat, 30.0f }, { p::tight, 30.0f } } },

            // Teaches: parallel processing — a deliberately crushed copy blended in at 35 % Mix.
            { "Drum Room Crush", "Drums", {
                { p::sauceAmt, 65.0f }, { p::programMode, 2.0f }, { p::character, 0.0f },
                { p::console, 3.0f }, { p::heat, 60.0f }, { p::punch, 50.0f },
                { p::density, 55.0f }, { p::snap, 30.0f }, { p::tight, 25.0f },
                { p::mix, 35.0f } } },

            // Teaches: Tight + Emerald thicken lows that still translate; Warmth is a nudge, not a boost.
            { "Bass Foundation", "Bass", {
                { p::sauceAmt, 40.0f }, { p::programMode, 3.0f }, { p::console, 4.0f },
                { p::heat, 35.0f }, { p::tight, 45.0f }, { p::warmth, 10.0f } } },

            // Teaches: negative Body cleans boom while Sparkle opens the top — bipolar EQ cuts too.
            { "Acoustic Guitar Sparkle", "Guitar", {
                { p::programMode, 4.0f }, { p::character, 0.0f }, { p::console, 2.0f },
                { p::heat, 12.0f }, { p::body, -12.0f }, { p::sparkle, 18.0f },
                { p::air, 12.0f }, { p::tight, 15.0f }, { p::width, 105.0f } } },

            // Teaches: Copper's transformer weight glues a doubled guitar wall into one instrument.
            { "Electric Guitar Bus", "Guitar", {
                { p::sauceAmt, 40.0f }, { p::programMode, 4.0f }, { p::character, 0.0f },
                { p::console, 6.0f }, { p::heat, 30.0f }, { p::glue, 20.0f },
                { p::presence, 12.0f }, { p::body, 8.0f }, { p::width, 110.0f } } },

            // Teaches: less Sauce than default is a valid move — Vintage bloom for felt and hammers.
            { "Piano & Keys Bloom", "Keys", {
                { p::sauceAmt, 30.0f }, { p::programMode, 5.0f }, { p::character, 0.0f },
                { p::heat, 15.0f }, { p::glue, 15.0f }, { p::warmth, 10.0f },
                { p::air, 12.0f }, { p::width, 110.0f } } },

            // Teaches: Width + Sparkle sheen stays mono-safe — Bass Mono (default 120 Hz) guards the low end.
            { "Synth Pop Sheen", "Synth", {
                { p::sauceAmt, 45.0f }, { p::programMode, 6.0f }, { p::console, 5.0f },
                { p::heat, 15.0f }, { p::air, 15.0f }, { p::sparkle, 20.0f },
                { p::snap, 10.0f }, { p::width, 120.0f } } },

            // Teaches: on a whole mix, small moves everywhere beat one big move anywhere.
            { "Mix Bus Finisher", "Bus", {
                { p::sauceAmt, 40.0f }, { p::programMode, 7.0f }, { p::glue, 25.0f },
                { p::heat, 18.0f }, { p::air, 12.0f }, { p::tight, 15.0f },
                { p::width, 108.0f } } },

            // Teaches: gentle streaming-safe mastering — modest Wow, ceiling stays at the -1 dBTP default.
            { "Master Polish", "Master", {
                { p::sauceAmt, 25.0f }, { p::programMode, 8.0f }, { p::console, 5.0f },
                { p::heat, 10.0f }, { p::loud, 25.0f }, { p::air, 8.0f },
                { p::tight, 10.0f } } },

            // Teaches: Wow + Ceiling are the limiter; Auto Gain off because loud IS the point here.
            { "Club Master (Loud)", "Master", {
                { p::sauceAmt, 45.0f }, { p::programMode, 8.0f }, { p::console, 4.0f },
                { p::heat, 25.0f }, { p::loud, 65.0f }, { p::ceiling, -0.3f },
                { p::glue, 20.0f }, { p::tight, 35.0f }, { p::width, 105.0f },
                { p::autoGain, 0.0f } } },
        };

        return presets;
    }

    /** Reset every parameter to default, then apply the preset's values. */
    inline void applyPreset (juce::AudioProcessorValueTreeState& apvts, const Preset& preset)
    {
        if (auto* um = apvts.undoManager)
            um->beginNewTransaction (juce::String ("Load preset: ") + preset.name);

        // Start from defaults so presets fully describe themselves.
        for (auto* param : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                ranged->beginChangeGesture();
                ranged->setValueNotifyingHost (ranged->getDefaultValue());
                ranged->endChangeGesture();
            }

        for (const auto& [id, value] : preset.values)
            if (auto* param = apvts.getParameter (id))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 (value));
                param->endChangeGesture();
            }
    }
} // namespace sauce::state
