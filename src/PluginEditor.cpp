#include "PluginEditor.h"
#include "state/Presets.h"

namespace p = sauce::param;
using namespace sauce::ui;

//==============================================================================
SecretSauceEditor::SecretSauceEditor (SecretSauceProcessor& proc)
    : AudioProcessorEditor (proc), processor (proc)
{
    setLookAndFeel (&lnf);

    // --- Header ---------------------------------------------------------------
    title.setText ("THE SECRET SAUCE", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, colours::gold);
    addAndMakeVisible (title);

    buildPresetMenu();
    addAndMakeVisible (presetBox);

    for (int i = 0; i < 4; ++i)
    {
        auto& b = snapButtons[i];
        b.setButtonText (juce::String::charToString (juce::juce_wchar ('A' + i)));
        b.setClickingTogglesState (false);
        b.onClick = [this, i]
        {
            if (juce::ModifierKeys::currentModifiers.isAnyModifierKeyDown())
                processor.snapshots.copyCurrentTo (i);   // modifier-click stores
            else
                processor.snapshots.recall (i);
        };
        b.setTooltip ("Click: recall snapshot. Shift/Cmd-click: store current settings here.");
        addAndMakeVisible (b);
    }

    undoButton.onClick = [this] { processor.undoManager.undo(); };
    redoButton.onClick = [this] { processor.undoManager.redo(); };
    driftButton.onClick = [this] { processor.rerollDriftSeed(); };
    driftButton.setTooltip ("New unit off the production line: re-roll the analog drift seed.");
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);
    addAndMakeVisible (driftButton);

    // --- Hero + recipe ----------------------------------------------------------
    addKnob (sauceKnob, "SAUCE", p::sauceAmt, true);

    characterBox.addItemList (p::characterChoices, 1);
    consoleBox.addItemList (p::consoleChoices, 1);
    programBox.addItemList (p::programChoices, 1);
    characterAtt = std::make_unique<ComboAttachment> (processor.apvts, p::character, characterBox);
    consoleAtt   = std::make_unique<ComboAttachment> (processor.apvts, p::console, consoleBox);
    programAtt   = std::make_unique<ComboAttachment> (processor.apvts, p::programMode, programBox);
    addAndMakeVisible (characterBox);
    addAndMakeVisible (consoleBox);
    addAndMakeVisible (programBox);

    detectedLabel.setJustificationType (juce::Justification::centred);
    detectedLabel.setColour (juce::Label::textColourId, colours::textDim);
    detectedLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (detectedLabel);

    // --- Tone -------------------------------------------------------------------
    addKnob (warmthKnob,   "Warmth",   p::warmth);
    addKnob (airKnob,      "Air",      p::air);
    addKnob (bodyKnob,     "Body",     p::body);
    addKnob (presenceKnob, "Presence", p::presence);
    addKnob (weightKnob,   "Weight",   p::weight);
    addKnob (sparkleKnob,  "Sparkle",  p::sparkle);

    // --- Dynamics & colour --------------------------------------------------------
    addKnob (glueKnob,    "Glue",    p::glue);
    addKnob (punchKnob,   "Punch",   p::punch);
    addKnob (densityKnob, "Density", p::density);
    addKnob (snapKnob,    "Snap",    p::snap);
    addKnob (heatKnob,    "Heat",    p::heat);
    addKnob (widthKnob,   "Width",   p::width);
    addKnob (tightKnob,   "Tight",   p::tight);
    addKnob (loudKnob,    "Wow",     p::loud);

    // --- Footer ----------------------------------------------------------------
    addKnob (mixKnob,    "Mix",    p::mix);
    addKnob (outputKnob, "Output", p::output);
    addKnob (inTrimKnob, "Input",  p::inTrim);

    osBox.addItemList (p::osChoices, 1);
    osAtt = std::make_unique<ComboAttachment> (processor.apvts, p::oversampling, osBox);
    addAndMakeVisible (osBox);

    auto attachToggle = [this] (juce::ToggleButton& b, const char* id,
                                std::unique_ptr<ButtonAttachment>& att)
    {
        att = std::make_unique<ButtonAttachment> (processor.apvts, id, b);
        addAndMakeVisible (b);
    };

    attachToggle (autoGainToggle, p::autoGain, autoGainAtt);
    attachToggle (deltaToggle,    p::delta,    deltaAtt);
    attachToggle (monoSafeToggle, p::monoSafe, monoSafeAtt);
    attachToggle (linPhaseToggle, p::linearPhase, linPhaseAtt);
    attachToggle (extScToggle,    p::extSidechain, extScAtt);
    attachToggle (bypassToggle,   p::bypass,   bypassAtt);

    // --- Meters -----------------------------------------------------------------
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    for (auto* l : { &grLabel, &loudnessLabel, &corrLabel })
    {
        l->setJustificationType (juce::Justification::centredLeft);
        l->setColour (juce::Label::textColourId, colours::textDim);
        l->setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (*l);
    }

    setSize (860, 560);
    startTimerHz (30);
}

SecretSauceEditor::~SecretSauceEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void SecretSauceEditor::buildPresetMenu()
{
    presetBox.clear();
    presetBox.setTextWhenNothingSelected ("Presets…");

    int id = 1;
    for (const auto& preset : sauce::state::factoryPresets())
        presetBox.addItem (juce::String (preset.category) + ": " + preset.name, id++);

    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        const auto& presets = sauce::state::factoryPresets();

        if (juce::isPositiveAndBelow (index, (int) presets.size()))
            sauce::state::applyPreset (processor.apvts, presets[(size_t) index]);
    };
}

void SecretSauceEditor::addKnob (Knob& knob, const juce::String& name, const char* paramID, bool hero)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, hero ? 90 : 64, 16);
    if (hero)
        knob.slider.getProperties().set ("hero", true);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (hero ? 16.0f : 12.0f, hero ? juce::Font::bold : juce::Font::plain));
    knob.label.setColour (juce::Label::textColourId, hero ? colours::gold : colours::textDim);

    knob.attachment = std::make_unique<SliderAttachment> (processor.apvts, paramID, knob.slider);

    addAndMakeVisible (knob.slider);
    addAndMakeVisible (knob.label);
}

//==============================================================================
void SecretSauceEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Panel separators
    g.setColour (colours::panelLine);
    g.drawHorizontalLine (52, 8.0f, (float) getWidth() - 8.0f);
    g.drawHorizontalLine (getHeight() - 118, 8.0f, (float) getWidth() - 8.0f);
}

void SecretSauceEditor::layoutKnobRow (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs)
{
    if (knobs.size() == 0)
        return;

    const int w = area.getWidth() / (int) knobs.size();
    for (auto* k : knobs)
    {
        auto cell = area.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell);
    }
}

void SecretSauceEditor::resized()
{
    auto area = getLocalBounds().reduced (8);

    // --- Header ----------------------------------------------------------------
    auto header = area.removeFromTop (44);
    title.setBounds (header.removeFromLeft (240));
    presetBox.setBounds (header.removeFromLeft (200).reduced (0, 8));
    header.removeFromLeft (12);

    for (auto& b : snapButtons)
        b.setBounds (header.removeFromLeft (34).reduced (2, 8));

    header.removeFromLeft (8);
    undoButton.setBounds (header.removeFromLeft (56).reduced (2, 8));
    redoButton.setBounds (header.removeFromLeft (56).reduced (2, 8));
    driftButton.setBounds (header.removeFromRight (70).reduced (2, 8));

    // --- Footer ----------------------------------------------------------------
    auto footer = area.removeFromBottom (110);
    footer.removeFromTop (8);
    {
        auto strip = footer;
        auto knobArea = strip.removeFromLeft (300);
        layoutKnobRow (knobArea, { &inTrimKnob, &mixKnob, &outputKnob });

        strip.removeFromLeft (10);
        auto toggles = strip.removeFromLeft (240);
        const int rowH = toggles.getHeight() / 3;
        auto r1 = toggles.removeFromTop (rowH);
        auto r2 = toggles.removeFromTop (rowH);
        auto r3 = toggles;
        autoGainToggle.setBounds (r1.removeFromLeft (120));
        deltaToggle.setBounds (r1);
        monoSafeToggle.setBounds (r2.removeFromLeft (120));
        linPhaseToggle.setBounds (r2);
        extScToggle.setBounds (r3.removeFromLeft (120));
        bypassToggle.setBounds (r3);

        strip.removeFromLeft (10);
        auto osArea = strip.removeFromLeft (120);
        osBox.setBounds (osArea.removeFromTop (26));

        strip.removeFromLeft (10);
        grLabel.setBounds (strip.removeFromTop (24));
        loudnessLabel.setBounds (strip.removeFromTop (24));
        corrLabel.setBounds (strip.removeFromTop (24));
    }

    // --- Main area ---------------------------------------------------------------
    area.removeFromTop (6);
    inMeter.setBounds (area.removeFromLeft (26).reduced (2));
    outMeter.setBounds (area.removeFromRight (26).reduced (2));
    area.reduce (10, 0);

    // Centre column: hero knob + combos
    auto centre = area.removeFromLeft (juce::jmax (220, area.getWidth() / 3));
    auto combos = centre.removeFromBottom (96);
    sauceKnob.label.setBounds (centre.removeFromTop (20));
    sauceKnob.slider.setBounds (centre.reduced (4));

    auto comboRow1 = combos.removeFromTop (26);
    characterBox.setBounds (comboRow1.removeFromLeft (combos.getWidth() / 2).reduced (2, 0));
    consoleBox.setBounds (comboRow1.reduced (2, 0));
    auto comboRow2 = combos.removeFromTop (26);
    programBox.setBounds (comboRow2.reduced (2, 0));
    detectedLabel.setBounds (combos.removeFromTop (20));

    // Right side: tone + dynamics grids
    area.removeFromLeft (10);
    const int rowH = area.getHeight() / 3;
    layoutKnobRow (area.removeFromTop (rowH), { &warmthKnob, &airKnob, &bodyKnob,
                                                &presenceKnob, &weightKnob, &sparkleKnob });
    layoutKnobRow (area.removeFromTop (rowH), { &glueKnob, &punchKnob, &densityKnob, &snapKnob });
    layoutKnobRow (area,                      { &heatKnob, &widthKnob, &tightKnob, &loudKnob });
}

//==============================================================================
void SecretSauceEditor::StereoMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 3.0f);

    auto drawBar = [&] (juce::Rectangle<float> r, float level, float peak)
    {
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float norm = juce::jmap (db, -60.0f, 3.0f, 0.0f, 1.0f);
        auto fill = r.removeFromBottom (r.getHeight() * juce::jlimit (0.0f, 1.0f, norm));

        g.setColour (db > 0.0f ? colours::meterRed
                    : db > -12.0f ? colours::meterAmber : colours::meterGreen);
        g.fillRect (fill);

        const float peakDb = juce::Decibels::gainToDecibels (peak, -60.0f);
        const float peakNorm = juce::jlimit (0.0f, 1.0f, juce::jmap (peakDb, -60.0f, 3.0f, 0.0f, 1.0f));
        g.setColour (colours::text.withAlpha (0.8f));
        const float y = getHeight() * (1.0f - peakNorm);
        g.fillRect (juce::Rectangle<float> (r.getX(), y, r.getWidth(), 1.5f));
    };

    auto inner = getLocalBounds().toFloat().reduced (2.0f);
    auto left  = inner.removeFromLeft (inner.getWidth() * 0.5f).reduced (1.0f, 0.0f);
    auto right = inner.reduced (1.0f, 0.0f);
    drawBar (left, levelL, peakL);
    drawBar (right, levelR, peakR);
}

void SecretSauceEditor::timerCallback()
{
    auto& m = processor.meters;

    auto decay = [] (float& stored, float incoming)
    {
        stored = incoming > stored ? incoming : stored * 0.86f;
    };

    decay (inMeter.levelL,  m.inRms[0].load (std::memory_order_relaxed));
    decay (inMeter.levelR,  m.inRms[1].load (std::memory_order_relaxed));
    decay (inMeter.peakL,   m.inPeak[0].load (std::memory_order_relaxed));
    decay (inMeter.peakR,   m.inPeak[1].load (std::memory_order_relaxed));
    decay (outMeter.levelL, m.outRms[0].load (std::memory_order_relaxed));
    decay (outMeter.levelR, m.outRms[1].load (std::memory_order_relaxed));
    decay (outMeter.peakL,  m.outPeak[0].load (std::memory_order_relaxed));
    decay (outMeter.peakR,  m.outPeak[1].load (std::memory_order_relaxed));
    inMeter.repaint();
    outMeter.repaint();

    grLabel.setText (juce::String::formatted ("GR  dyn %.1f dB   lim %.1f dB",
                                              m.grDynamicsDb.load (std::memory_order_relaxed),
                                              m.grLimiterDb.load (std::memory_order_relaxed)),
                     juce::dontSendNotification);

    loudnessLabel.setText (juce::String::formatted ("LUFS  in %.1f   out %.1f   AG %+.1f dB",
                                                    m.inLufs.load (std::memory_order_relaxed),
                                                    m.outLufs.load (std::memory_order_relaxed),
                                                    m.autoGainDb.load (std::memory_order_relaxed)),
                           juce::dontSendNotification);

    corrLabel.setText (juce::String::formatted ("Correlation %+.2f",
                                                m.correlation.load (std::memory_order_relaxed)),
                       juce::dontSendNotification);

    const int prog = m.detectedProgram.load (std::memory_order_relaxed);
    if (juce::isPositiveAndBelow (prog, p::programChoices.size()))
        detectedLabel.setText ("Sensing: " + p::programChoices[prog], juce::dontSendNotification);

    for (int i = 0; i < 4; ++i)
        snapButtons[i].setToggleState (processor.snapshots.getActive() == i, juce::dontSendNotification);
}
