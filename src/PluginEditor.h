#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

// =============================================================================
// The Secret Sauce — editor.
//
// Layout:
//   header : logo, preset browser, A/B/C/D snapshots, undo/redo, drift re-roll
//   left   : input meter
//   centre : hero SAUCE knob, recipe (Vintage/Modern), console, program
//            tone macros (Warmth/Air/Body/Presence/Weight/Sparkle)
//            dynamics macros (Glue/Punch/Density/Snap) + Heat/Width/Tight/Wow
//   right  : output meter, GR meters, correlation
//   footer : Mix, Output, Auto Gain, Delta, Mono Safe, Oversampling,
//            Linear Phase, sidechain controls, Bypass
// =============================================================================
class SecretSauceEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit SecretSauceEditor (SecretSauceProcessor&);
    ~SecretSauceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildPresetMenu();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void addKnob (Knob& knob, const juce::String& name, const char* paramID, bool hero = false);
    void layoutKnobRow (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs);

    // --- Meter component ------------------------------------------------------
    struct StereoMeter : juce::Component
    {
        void paint (juce::Graphics& g) override;
        float levelL = 0.0f, levelR = 0.0f, peakL = 0.0f, peakR = 0.0f;
    };

    SecretSauceProcessor& processor;
    sauce::ui::SauceLookAndFeel lnf;

    // Header
    juce::Label title;
    juce::ComboBox presetBox;
    juce::TextButton snapButtons[4];
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton driftButton { "Re-roll" };

    // Hero + recipe
    Knob sauceKnob;
    juce::ComboBox characterBox, consoleBox, programBox;
    std::unique_ptr<ComboAttachment> characterAtt, consoleAtt, programAtt;
    juce::Label detectedLabel;

    // Tone
    Knob warmthKnob, airKnob, bodyKnob, presenceKnob, weightKnob, sparkleKnob;

    // Dynamics & colour
    Knob glueKnob, punchKnob, densityKnob, snapKnob;
    Knob heatKnob, widthKnob, tightKnob, loudKnob;

    // Footer
    Knob mixKnob, outputKnob, inTrimKnob;
    juce::ToggleButton autoGainToggle { "Auto Gain" }, deltaToggle { "Delta" },
                       monoSafeToggle { "Mono Safe" }, linPhaseToggle { "Lin Phase" },
                       extScToggle { "Ext SC" }, bypassToggle { "Bypass" };
    juce::ComboBox osBox;
    std::unique_ptr<ComboAttachment> osAtt;
    std::unique_ptr<ButtonAttachment> autoGainAtt, deltaAtt, monoSafeAtt, linPhaseAtt,
                                      extScAtt, bypassAtt;

    // Meters
    StereoMeter inMeter, outMeter;
    juce::Label grLabel, loudnessLabel, corrLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SecretSauceEditor)
};
