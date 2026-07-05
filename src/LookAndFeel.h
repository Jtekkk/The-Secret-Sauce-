#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sauce::ui
{
    // =========================================================================
    // "Laboratory kitchen" look: dark slate, copper accents, one hero knob.
    // =========================================================================
    namespace colours
    {
        const juce::Colour background  { 0xff14161a };
        const juce::Colour panel       { 0xff1d2026 };
        const juce::Colour panelLine   { 0xff2a2e36 };
        const juce::Colour text        { 0xffe8e2d6 };
        const juce::Colour textDim     { 0xff8a8578 };
        const juce::Colour sauceRed    { 0xffd8482f };
        const juce::Colour copper      { 0xffc98a52 };
        const juce::Colour gold        { 0xffe0b45c };
        const juce::Colour meterGreen  { 0xff69b578 };
        const juce::Colour meterAmber  { 0xffdba54a };
        const juce::Colour meterRed    { 0xffd8482f };
    }

    class SauceLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        SauceLookAndFeel()
        {
            setColour (juce::ResizableWindow::backgroundColourId, colours::background);
            setColour (juce::Slider::textBoxTextColourId, colours::text);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::rotarySliderFillColourId, colours::copper);
            setColour (juce::Slider::rotarySliderOutlineColourId, colours::panelLine);
            setColour (juce::Slider::thumbColourId, colours::gold);
            setColour (juce::Label::textColourId, colours::text);
            setColour (juce::ComboBox::backgroundColourId, colours::panel);
            setColour (juce::ComboBox::textColourId, colours::text);
            setColour (juce::ComboBox::outlineColourId, colours::panelLine);
            setColour (juce::ComboBox::arrowColourId, colours::copper);
            setColour (juce::PopupMenu::backgroundColourId, colours::panel);
            setColour (juce::PopupMenu::textColourId, colours::text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::sauceRed.withAlpha (0.35f));
            setColour (juce::TextButton::buttonColourId, colours::panel);
            setColour (juce::TextButton::buttonOnColourId, colours::sauceRed.withAlpha (0.7f));
            setColour (juce::TextButton::textColourOffId, colours::text);
            setColour (juce::TextButton::textColourOnId, colours::text);
            setColour (juce::ToggleButton::textColourId, colours::text);
            setColour (juce::ToggleButton::tickColourId, colours::copper);
            setColour (juce::TooltipWindow::backgroundColourId, colours::panel);
            setColour (juce::TooltipWindow::textColourId, colours::text);
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                               juce::Slider& slider) override
        {
            auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto centre = bounds.getCentre();
            const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
            const bool isHero = slider.getProperties().contains ("hero");
            const float lineW = isHero ? 5.0f : 3.0f;
            const float arcRadius = radius - lineW * 0.5f;

            // Body
            g.setColour (colours::panel.brighter (0.06f));
            g.fillEllipse (centre.x - arcRadius + lineW, centre.y - arcRadius + lineW,
                           (arcRadius - lineW) * 2.0f, (arcRadius - lineW) * 2.0f);

            // Track
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (colours::panelLine);
            g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            // Value arc — bipolar sliders fill from centre.
            const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
            const float startAngle = bipolar
                ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle)
                : rotaryStartAngle;

            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 startAngle, angle, true);
            g.setColour (isHero ? colours::sauceRed : colours::copper);
            g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            // Pointer
            juce::Path pointer;
            const float pointerLen = arcRadius * 0.55f;
            pointer.startNewSubPath (centre.getPointOnCircumference (arcRadius - lineW - 2.0f, angle));
            pointer.lineTo (centre.getPointOnCircumference (arcRadius - lineW - 2.0f - pointerLen, angle));
            g.setColour (isHero ? colours::gold : colours::text);
            g.strokePath (pointer, juce::PathStrokeType (isHero ? 3.5f : 2.0f,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }
    };
} // namespace sauce::ui
