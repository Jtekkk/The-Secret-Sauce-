#pragma once

#include "PluginProcessor.h"
#include "LookAndFeel.h"

// =============================================================================
// The Secret Sauce — editor.
//
// Layout (resizable, 760x520 .. 1280x840):
//   header : flask logo + wordmark, preset browser (grouped by category),
//            A/B/C/D snapshots, undo/redo, drift re-roll
//   left   : input stereo meter
//   centre : "THE SAUCE" panel — hero knob (value readout inside) over the
//            recipe controls (Vintage/Modern, Console, Program, Sensing)
//   right  : three framed sections — FREQUENCY MAGIC, MICRO DYNAMICS,
//            COLOUR & SPACE
//   right edge : output stereo meter
//   footer : OUTPUT STAGE (Input/Mix/Output), CONTROL (toggles + oversampling)
//            and TELEMETRY (GR bars, correlation bar, LUFS, status line)
//
// Cmd/Ctrl+Z undoes, Shift+Cmd/Ctrl+Z (or Cmd/Ctrl+Y) redoes. Engaging Bypass
// dims the whole UI behind a translucent overlay.
// =============================================================================
class SecretSauceEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit SecretSauceEditor (SecretSauceProcessor&);
    ~SecretSauceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

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

    // Small horizontal gain-reduction bar (0..12 dB, fills right-to-left).
    struct GainBar : juce::Component,
                     public juce::SettableTooltipClient
    {
        void paint (juce::Graphics& g) override;
        juce::String caption, valueText;
        float norm = 0.0f;
    };

    // Correlation bar (-1..+1) with a centre mark; fills out from the centre.
    struct CorrelationBar : juce::Component,
                            public juce::SettableTooltipClient
    {
        void paint (juce::Graphics& g) override;
        float value = 1.0f;
    };

    // Translucent shade shown while Bypass is engaged (clicks pass through).
    struct DimOverlay : juce::Component
    {
        DimOverlay() { setInterceptsMouseClicks (false, false); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (sauce::ui::colours::background.withAlpha (0.55f));
        }
    };

    // Painted section frame (rounded panel + small uppercase title).
    struct Section
    {
        juce::Rectangle<int> bounds;
        juce::String title;
    };

    SecretSauceProcessor& processor;
    sauce::ui::SauceLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 400 };

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
    juce::Label osLabel;
    std::unique_ptr<ComboAttachment> osAtt;
    std::unique_ptr<ButtonAttachment> autoGainAtt, deltaAtt, monoSafeAtt, linPhaseAtt,
                                      extScAtt, bypassAtt;

    // Meters & readouts
    StereoMeter inMeter, outMeter;
    GainBar grDynBar, grLimBar;
    CorrelationBar corrBar;
    juce::Label loudnessLabel, statusLabel;

    // Painted chrome, recomputed in resized().
    std::vector<Section> sections;
    juce::Rectangle<int> flaskArea, headerRule, inCaption, outCaption;

    DimOverlay bypassOverlay;
    std::atomic<float>* bypassValue = nullptr;
    int snapStyle[4] { -1, -1, -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SecretSauceEditor)
};
