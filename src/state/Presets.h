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
            { "Default", "Init", {
                { p::sauceAmt, 35.0f }, { p::heat, 20.0f }, { p::mix, 100.0f } } },

            { "Mix Bus Finisher", "Bus", {
                { p::sauceAmt, 40.0f }, { p::programMode, 7.0f }, { p::character, 1.0f },
                { p::console, 1.0f }, { p::glue, 25.0f }, { p::heat, 18.0f },
                { p::air, 12.0f }, { p::tight, 15.0f }, { p::width, 108.0f } } },

            { "Master Polish", "Master", {
                { p::sauceAmt, 25.0f }, { p::programMode, 8.0f }, { p::character, 1.0f },
                { p::console, 5.0f }, { p::heat, 10.0f }, { p::loud, 30.0f },
                { p::ceiling, -1.0f }, { p::air, 8.0f }, { p::tight, 10.0f } } },

            { "Vocal Silk", "Vocals", {
                { p::sauceAmt, 45.0f }, { p::programMode, 1.0f }, { p::character, 0.0f },
                { p::console, 2.0f }, { p::heat, 25.0f }, { p::air, 25.0f },
                { p::presence, 15.0f }, { p::density, 30.0f }, { p::width, 95.0f } } },

            { "Drum Bus Glue", "Drums", {
                { p::sauceAmt, 50.0f }, { p::programMode, 2.0f }, { p::character, 0.0f },
                { p::console, 3.0f }, { p::glue, 40.0f }, { p::punch, 35.0f },
                { p::snap, 25.0f }, { p::heat, 30.0f }, { p::tight, 30.0f } } },

            { "Bass Foundation", "Bass", {
                { p::sauceAmt, 40.0f }, { p::programMode, 3.0f }, { p::console, 4.0f },
                { p::heat, 35.0f }, { p::tight, 45.0f }, { p::width, 100.0f },
                { p::monoSafe, 1.0f }, { p::warmth, 10.0f } } },
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
