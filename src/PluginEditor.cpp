#include "PluginEditor.h"
#include "state/Presets.h"

namespace p = sauce::param;
using namespace sauce::ui;

//==============================================================================
SecretSauceEditor::SecretSauceEditor (SecretSauceProcessor& proc)
    : AudioProcessorEditor (proc), processor (proc)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);

    // --- Header ---------------------------------------------------------------
    title.setText ("THE SECRET SAUCE", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::bold))
                       .withExtraKerningFactor (0.04f));
    title.setColour (juce::Label::textColourId, colours::gold);
    title.setJustificationType (juce::Justification::centredLeft);
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
    undoButton.setTooltip ("Undo (Cmd/Ctrl+Z)");
    redoButton.setTooltip ("Redo (Shift+Cmd/Ctrl+Z)");
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
    detectedLabel.setColour (juce::Label::textColourId, colours::copper);
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

    osLabel.setText ("OVERSAMPLE", juce::dontSendNotification);
    osLabel.setFont (sectionTitleFont().withHeight (9.0f));
    osLabel.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (osLabel);

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

    deltaToggle.getProperties().set ("pill", true);
    bypassToggle.getProperties().set ("pill", true);
    deltaToggle.setTooltip ("Monitor only the difference the plugin is making.");
    bypassToggle.setTooltip ("True bypass (latency-compensated).");

    // --- Meters & readouts --------------------------------------------------------
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    grDynBar.caption = "DYN";
    grLimBar.caption = "LIM";
    grDynBar.setTooltip ("Micro-dynamics gain reduction (bar spans 0-12 dB).");
    grLimBar.setTooltip ("Limiter gain reduction (bar spans 0-12 dB).");
    corrBar.setTooltip ("Stereo correlation: +1 is mono-compatible, below 0 means phase trouble.");
    addAndMakeVisible (grDynBar);
    addAndMakeVisible (grLimBar);
    addAndMakeVisible (corrBar);

    for (auto* l : { &loudnessLabel, &statusLabel })
    {
        l->setJustificationType (juce::Justification::centredLeft);
        l->setColour (juce::Label::textColourId, colours::textDim);
        l->setFont (juce::FontOptions (10.5f));
        addAndMakeVisible (*l);
    }

    addChildComponent (bypassOverlay);
    bypassValue = processor.apvts.getRawParameterValue (p::bypass);

    setResizable (true, true);
    setResizeLimits (760, 520, 1280, 840);
    setSize (960, 640);
    startTimerHz (30);
}

SecretSauceEditor::~SecretSauceEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void SecretSauceEditor::buildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.setTextWhenNothingSelected ("Presets…");

    int id = 1;
    juce::String lastCategory;

    for (const auto& preset : sauce::state::factoryPresets())
    {
        if (lastCategory != preset.category)
        {
            lastCategory = preset.category;
            presetBox.addSectionHeading (lastCategory);
        }

        presetBox.addItem (preset.name, id++);
    }

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

    if (hero)
    {
        knob.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob.slider.getProperties().set ("hero", true);
    }
    else
    {
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 14);
    }

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (hero ? 15.0f : 12.0f, hero ? juce::Font::bold : juce::Font::plain));
    knob.label.setColour (juce::Label::textColourId, hero ? colours::gold : colours::textDim);

    knob.attachment = std::make_unique<SliderAttachment> (processor.apvts, paramID, knob.slider);

    addAndMakeVisible (knob.slider);
    addAndMakeVisible (knob.label);
}

//==============================================================================
bool SecretSauceEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    if (mods.isCommandDown())
    {
        const int code = key.getKeyCode();

        if (code == 'Z' || code == 'z')
        {
            if (mods.isShiftDown())
                processor.undoManager.redo();
            else
                processor.undoManager.undo();

            return true;
        }

        if (code == 'Y' || code == 'y')
        {
            processor.undoManager.redo();
            return true;
        }
    }

    return false;
}

//==============================================================================
void SecretSauceEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Section frames
    for (const auto& s : sections)
    {
        auto r = s.bounds.toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colours::panelLine);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        if (s.title.isNotEmpty())
        {
            g.setColour (colours::textDim);
            g.setFont (sectionTitleFont());
            g.drawText (s.title, s.bounds.getX() + 12, s.bounds.getY() + 7,
                        s.bounds.getWidth() - 24, 12, juce::Justification::centredLeft, false);
        }
    }

    // Header rule
    g.setColour (colours::panelLine);
    g.fillRect (headerRule);

    // Flask logo: copper Erlenmeyer with sauce in the bottom.
    {
        auto f = flaskArea.toFloat();
        const float neckW = f.getWidth() * 0.34f;
        const float shoulderY = f.getY() + f.getHeight() * 0.38f;
        const float cx = f.getCentreX();

        juce::Path flask;
        flask.startNewSubPath (cx - neckW * 0.5f, f.getY());
        flask.lineTo (cx - neckW * 0.5f, shoulderY);
        flask.lineTo (f.getX(), f.getBottom() - 1.5f);
        flask.lineTo (f.getRight(), f.getBottom() - 1.5f);
        flask.lineTo (cx + neckW * 0.5f, shoulderY);
        flask.lineTo (cx + neckW * 0.5f, f.getY());

        g.saveState();
        g.reduceClipRegion (flask);
        g.setColour (colours::sauceRed);
        g.fillRect (f.withTop (f.getY() + f.getHeight() * 0.62f));
        g.restoreState();

        g.setColour (colours::copper);
        g.strokePath (flask, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Meter captions
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("IN", inCaption, juce::Justification::centred, false);
    g.drawText ("OUT", outCaption, juce::Justification::centred, false);
}

void SecretSauceEditor::layoutKnobRow (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs)
{
    if (knobs.size() == 0)
        return;

    const int w = area.getWidth() / (int) knobs.size();

    for (auto* k : knobs)
    {
        auto cell = area.removeFromLeft (w).reduced (2, 0);
        k->label.setBounds (cell.removeFromTop (14));
        k->slider.setBounds (cell);
    }
}

void SecretSauceEditor::resized()
{
    sections.clear();

    auto bounds = getLocalBounds().reduced (8);

    // --- Header ----------------------------------------------------------------
    auto header = bounds.removeFromTop (44);
    headerRule = { bounds.getX(), header.getBottom() + 4, bounds.getWidth(), 1 };

    flaskArea = header.removeFromLeft (24).reduced (2, 10);
    header.removeFromLeft (8);
    title.setBounds (header.removeFromLeft (juce::jmin (196, header.getWidth() / 3)));

    driftButton.setBounds (header.removeFromRight (70).reduced (2, 9));
    header.removeFromRight (4);

    const int presetW = juce::jlimit (140, 240, header.getWidth() - 250);
    presetBox.setBounds (header.removeFromLeft (presetW).reduced (0, 9));
    header.removeFromLeft (10);

    for (auto& b : snapButtons)
        b.setBounds (header.removeFromLeft (30).reduced (2, 9));

    header.removeFromLeft (8);
    undoButton.setBounds (header.removeFromLeft (52).reduced (2, 9));
    redoButton.setBounds (header.removeFromLeft (52).reduced (2, 9));

    bounds.removeFromTop (10);

    // --- Footer ----------------------------------------------------------------
    auto footer = bounds.removeFromBottom (juce::jlimit (118, 148, getHeight() / 5));
    bounds.removeFromBottom (8);

    {
        auto strip = footer;

        auto ioArea = strip.removeFromLeft (juce::jlimit (225, 285, strip.getWidth() * 27 / 100));
        sections.push_back ({ ioArea, "OUTPUT STAGE" });
        layoutKnobRow (ioArea.reduced (10, 8).withTrimmedTop (14),
                       { &inTrimKnob, &mixKnob, &outputKnob });

        strip.removeFromLeft (8);
        auto ctrlArea = strip.removeFromLeft (juce::jlimit (250, 330, strip.getWidth() * 46 / 100));
        sections.push_back ({ ctrlArea, "CONTROL" });

        auto ctrl = ctrlArea.reduced (12, 8).withTrimmedTop (14);
        auto osCol = ctrl.removeFromRight (juce::jmin (100, ctrl.getWidth() / 3));
        osCol.removeFromLeft (8);
        osLabel.setBounds (osCol.removeFromTop (14));
        osBox.setBounds (osCol.removeFromTop (24));

        const int toggleRowH = ctrl.getHeight() / 3;
        auto tr1 = ctrl.removeFromTop (toggleRowH);
        auto tr2 = ctrl.removeFromTop (toggleRowH);
        auto tr3 = ctrl;
        const int colW = tr1.getWidth() / 2;

        autoGainToggle.setBounds (tr1.removeFromLeft (colW));
        deltaToggle.setBounds (tr1.reduced (4, 1));
        monoSafeToggle.setBounds (tr2.removeFromLeft (colW));
        linPhaseToggle.setBounds (tr2);
        extScToggle.setBounds (tr3.removeFromLeft (colW));
        bypassToggle.setBounds (tr3.reduced (4, 1));

        strip.removeFromLeft (8);
        sections.push_back ({ strip, "TELEMETRY" });

        auto tele = strip.reduced (12, 8).withTrimmedTop (14);
        const int teleRowH = juce::jmax (14, tele.getHeight() / 5);
        grDynBar.setBounds (tele.removeFromTop (teleRowH));
        grLimBar.setBounds (tele.removeFromTop (teleRowH));
        corrBar.setBounds (tele.removeFromTop (teleRowH));
        loudnessLabel.setBounds (tele.removeFromTop (teleRowH));
        statusLabel.setBounds (tele.removeFromTop (teleRowH));
    }

    // --- Meters ------------------------------------------------------------------
    auto meterL = bounds.removeFromLeft (30);
    auto meterR = bounds.removeFromRight (30);
    inCaption = meterL.removeFromBottom (14);
    outCaption = meterR.removeFromBottom (14);
    inMeter.setBounds (meterL.reduced (5, 2));
    outMeter.setBounds (meterR.reduced (5, 2));
    bounds.reduce (8, 0);

    // --- Centre: the hero panel ----------------------------------------------------
    auto sauceArea = bounds.removeFromLeft (juce::jlimit (230, 330, bounds.getWidth() * 32 / 100));
    sections.push_back ({ sauceArea, "THE SAUCE" });

    auto sauceInner = sauceArea.reduced (12).withTrimmedTop (12);
    auto recipeArea = sauceInner.removeFromBottom (100);
    sauceKnob.label.setBounds (sauceInner.removeFromBottom (20));
    sauceKnob.slider.setBounds (sauceInner);

    auto recipeRow1 = recipeArea.removeFromTop (26);
    characterBox.setBounds (recipeRow1.removeFromLeft (recipeRow1.getWidth() / 2).reduced (3, 1));
    consoleBox.setBounds (recipeRow1.reduced (3, 1));
    recipeArea.removeFromTop (6);
    programBox.setBounds (recipeArea.removeFromTop (26).reduced (3, 1));
    recipeArea.removeFromTop (4);
    detectedLabel.setBounds (recipeArea.removeFromTop (16));

    bounds.removeFromLeft (8);

    // --- Right: three framed knob sections ------------------------------------------
    const int sectionGap = 8;
    const int sectionH = (bounds.getHeight() - 2 * sectionGap) / 3;

    auto toneArea = bounds.removeFromTop (sectionH);
    bounds.removeFromTop (sectionGap);
    auto dynArea = bounds.removeFromTop (sectionH);
    bounds.removeFromTop (sectionGap);
    auto colourArea = bounds;

    sections.push_back ({ toneArea, "FREQUENCY MAGIC" });
    sections.push_back ({ dynArea, "MICRO DYNAMICS" });
    sections.push_back ({ colourArea, "COLOUR & SPACE" });

    layoutKnobRow (toneArea.reduced (10, 6).withTrimmedTop (16),
                   { &warmthKnob, &airKnob, &bodyKnob, &presenceKnob, &weightKnob, &sparkleKnob });
    layoutKnobRow (dynArea.reduced (10, 6).withTrimmedTop (16),
                   { &glueKnob, &punchKnob, &densityKnob, &snapKnob });
    layoutKnobRow (colourArea.reduced (10, 6).withTrimmedTop (16),
                   { &heatKnob, &widthKnob, &tightKnob, &loudKnob });

    bypassOverlay.setBounds (getLocalBounds());
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

void SecretSauceEditor::GainBar::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.0f, 1.0f);
    auto capArea = b.removeFromLeft (34.0f);

    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText (caption, capArea, juce::Justification::centredLeft, false);

    const float barH = juce::jmin (9.0f, b.getHeight());
    auto track = b.withSizeKeepingCentre (b.getWidth(), barH);

    g.setColour (colours::background);
    g.fillRoundedRectangle (track, 3.0f);

    const float n = juce::jlimit (0.0f, 1.0f, norm);

    if (n > 0.003f)
    {
        auto fillArea = track;
        g.setColour (colours::sauceRed);
        g.fillRoundedRectangle (fillArea.removeFromRight (fillArea.getWidth() * n), 3.0f);
    }

    g.setColour (colours::text.withAlpha (0.75f));
    g.setFont (juce::FontOptions (8.5f));
    g.drawText (valueText, track.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
}

void SecretSauceEditor::CorrelationBar::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.0f, 1.0f);
    auto capArea = b.removeFromLeft (34.0f);

    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("CORR", capArea, juce::Justification::centredLeft, false);

    const float barH = juce::jmin (9.0f, b.getHeight());
    auto track = b.withSizeKeepingCentre (b.getWidth(), barH);

    g.setColour (colours::background);
    g.fillRoundedRectangle (track, 3.0f);

    const float v = juce::jlimit (-1.0f, 1.0f, value);
    const float cx = track.getCentreX();
    const float half = track.getWidth() * 0.5f;

    if (std::abs (v) > 0.005f)
    {
        g.setColour ((v >= 0.0f ? colours::meterGreen
                     : v > -0.35f ? colours::meterAmber : colours::meterRed).withAlpha (0.9f));

        if (v >= 0.0f)
            g.fillRect (juce::Rectangle<float> (cx, track.getY(), half * v, track.getHeight()));
        else
            g.fillRect (juce::Rectangle<float> (cx + half * v, track.getY(), half * -v, track.getHeight()));
    }

    // Centre (zero-correlation) mark
    g.setColour (colours::text.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> (cx - 0.75f, track.getY() - 1.5f, 1.5f, track.getHeight() + 3.0f));

    g.setColour (colours::text.withAlpha (0.75f));
    g.setFont (juce::FontOptions (8.5f));
    g.drawText (juce::String (v >= 0.0f ? "+" : "") + juce::String (v, 2),
                track.reduced (4.0f, 0.0f), juce::Justification::centredRight, false);
}

//==============================================================================
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

    const float grDyn = m.grDynamicsDb.load (std::memory_order_relaxed);
    const float grLim = m.grLimiterDb.load (std::memory_order_relaxed);
    grDynBar.norm = grDyn * (1.0f / 12.0f);
    grLimBar.norm = grLim * (1.0f / 12.0f);
    grDynBar.valueText = juce::String (grDyn, 1) + " dB";
    grLimBar.valueText = juce::String (grLim, 1) + " dB";
    grDynBar.repaint();
    grLimBar.repaint();

    corrBar.value = m.correlation.load (std::memory_order_relaxed);
    corrBar.repaint();

    loudnessLabel.setText (juce::String::formatted ("LUFS  in %.1f · out %.1f · AG %+.1f dB",
                                                    m.inLufs.load (std::memory_order_relaxed),
                                                    m.outLufs.load (std::memory_order_relaxed),
                                                    m.autoGainDb.load (std::memory_order_relaxed)),
                           juce::dontSendNotification);

    const int prog = m.detectedProgram.load (std::memory_order_relaxed);
    juce::String sensed ("Auto");
    if (juce::isPositiveAndBelow (prog, p::programChoices.size()))
        sensed = p::programChoices[prog];

    detectedLabel.setText ("Sensing: " + sensed, juce::dontSendNotification);

    juce::String osText = osBox.getText();
    if (osText.isEmpty())
        osText = "Auto";

    statusLabel.setText ("Sensing: " + sensed + " · " + osText + " OS · "
                             + juce::String (processor.getCurrentLatency()) + " smp latency",
                         juce::dontSendNotification);

    undoButton.setEnabled (processor.undoManager.canUndo());
    redoButton.setEnabled (processor.undoManager.canRedo());

    const int activeSlot = processor.snapshots.getActive();

    for (int i = 0; i < 4; ++i)
    {
        const bool isActive = activeSlot == i;
        const int style = isActive ? 2 : (processor.snapshots.hasData (i) ? 1 : 0);

        snapButtons[i].setToggleState (isActive, juce::dontSendNotification);

        if (style != snapStyle[i])
        {
            snapStyle[i] = style;
            auto& b = snapButtons[i];
            b.setColour (juce::TextButton::buttonOnColourId, colours::gold);
            b.setColour (juce::TextButton::textColourOnId, colours::background);
            b.setColour (juce::TextButton::buttonColourId,
                         style == 1 ? colours::panel.brighter (0.1f) : colours::panel.darker (0.25f));
            b.setColour (juce::TextButton::textColourOffId,
                         style == 1 ? colours::text : colours::textDim.withAlpha (0.7f));
        }
    }

    const bool bypassed = bypassValue != nullptr
                          && bypassValue->load (std::memory_order_relaxed) >= 0.5f;

    if (bypassOverlay.isVisible() != bypassed)
        bypassOverlay.setVisible (bypassed);
}
