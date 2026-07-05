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

    /** Small, letter-spaced uppercase font used for section titles. */
    inline juce::Font sectionTitleFont()
    {
        return juce::Font (juce::FontOptions (11.0f, juce::Font::bold))
                   .withExtraKerningFactor (0.12f);
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
            setColour (juce::PopupMenu::headerTextColourId, colours::copper);
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
            const bool isHero = slider.getProperties().contains ("hero");

            auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat()
                              .reduced (isHero ? 16.0f : 4.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto centre = bounds.getCentre();
            const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
            const float lineW = isHero ? juce::jmax (5.0f, radius * 0.055f) : 3.0f;
            const float arcRadius = radius - lineW * 0.5f;

            // Body
            const float bodyRadius = arcRadius - lineW - 2.0f;
            if (bodyRadius > 2.0f)
            {
                if (isHero)
                {
                    juce::ColourGradient grad (colours::panel.brighter (0.16f),
                                               centre.x, centre.y - bodyRadius,
                                               colours::panel.darker (0.25f),
                                               centre.x, centre.y + bodyRadius, false);
                    g.setGradientFill (grad);
                }
                else
                {
                    g.setColour (colours::panel.brighter (0.06f));
                }

                g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                               bodyRadius * 2.0f, bodyRadius * 2.0f);

                if (isHero)
                {
                    g.setColour (colours::copper.withAlpha (0.3f));
                    g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                                   bodyRadius * 2.0f, bodyRadius * 2.0f, 1.2f);
                }
            }

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

            // Subtle outer glow on the hero knob once there is any sauce at all.
            if (isHero && sliderPos > 0.004f)
            {
                g.setColour (colours::sauceRed.withAlpha (0.16f));
                g.strokePath (value, juce::PathStrokeType (lineW + 7.0f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
                g.setColour (colours::sauceRed.withAlpha (0.07f));
                g.strokePath (value, juce::PathStrokeType (lineW + 14.0f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            }

            g.setColour (isHero ? colours::sauceRed : colours::copper);
            g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            if (isHero)
            {
                // Tick marks at 0 / 50 / 100.
                g.setColour (colours::textDim);
                for (float t : { 0.0f, 0.5f, 1.0f })
                {
                    const float tickAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
                    const auto p1 = centre.getPointOnCircumference (radius + 3.0f, tickAngle);
                    const auto p2 = centre.getPointOnCircumference (radius + 9.0f, tickAngle);
                    g.drawLine ({ p1, p2 }, 1.6f);
                }

                // Pointer dot riding the inside of the arc.
                const auto dot = centre.getPointOnCircumference (arcRadius - lineW - 5.0f, angle);
                const float dotR = juce::jmax (3.0f, lineW * 0.65f);
                g.setColour (colours::gold);
                g.fillEllipse (dot.x - dotR, dot.y - dotR, dotR * 2.0f, dotR * 2.0f);

                // Value readout inside the knob.
                g.setColour (colours::text);
                g.setFont (juce::FontOptions (juce::jlimit (15.0f, 34.0f, radius * 0.4f),
                                              juce::Font::bold));
                g.drawText (juce::String (juce::roundToInt (slider.getValue())) + "%",
                            bounds, juce::Justification::centred, false);
            }
            else
            {
                // Pointer
                juce::Path pointer;
                const float pointerLen = arcRadius * 0.55f;
                pointer.startNewSubPath (centre.getPointOnCircumference (arcRadius - lineW - 2.0f, angle));
                pointer.lineTo (centre.getPointOnCircumference (arcRadius - lineW - 2.0f - pointerLen, angle));
                g.setColour (colours::text);
                g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
            }
        }

        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
        {
            const bool on = button.getToggleState();

            // "pill" toggles (Delta, Bypass): outlined capsule that lights sauce-red.
            if (button.getProperties().contains ("pill"))
            {
                auto pill = button.getLocalBounds().toFloat().reduced (2.0f, 3.0f);
                const float corner = pill.getHeight() * 0.5f;

                if (on)
                {
                    g.setColour (colours::sauceRed.withAlpha (shouldDrawButtonAsDown ? 0.95f : 0.8f));
                    g.fillRoundedRectangle (pill, corner);
                }
                else if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
                {
                    g.setColour (colours::sauceRed.withAlpha (0.12f));
                    g.fillRoundedRectangle (pill, corner);
                }

                g.setColour (on ? colours::sauceRed.brighter (0.25f)
                                : colours::textDim.withAlpha (shouldDrawButtonAsHighlighted ? 0.9f : 0.55f));
                g.drawRoundedRectangle (pill.reduced (0.6f), corner, 1.2f);

                g.setColour (on ? colours::text : colours::textDim);
                g.setFont (juce::FontOptions (12.0f, on ? juce::Font::bold : juce::Font::plain));
                g.drawText (button.getButtonText(), pill, juce::Justification::centred, false);
                return;
            }

            // Standard toggles: small rounded box with a copper tick.
            auto bounds = button.getLocalBounds().toFloat();
            const float boxSize = juce::jmin (15.0f, bounds.getHeight() - 4.0f);
            auto box = juce::Rectangle<float> (boxSize, boxSize)
                           .withCentre ({ bounds.getX() + 2.0f + boxSize * 0.5f, bounds.getCentreY() });

            g.setColour (on ? colours::copper : colours::panel.brighter (0.05f));
            g.fillRoundedRectangle (box, 3.5f);
            g.setColour (on ? colours::copper.brighter (0.2f)
                            : (shouldDrawButtonAsHighlighted ? colours::textDim : colours::panelLine));
            g.drawRoundedRectangle (box, 3.5f, 1.0f);

            if (on)
            {
                auto tick = getTickShape (0.75f);
                g.setColour (colours::background);
                g.fillPath (tick, tick.getTransformToScaleToFit (box.reduced (3.0f), true));
            }

            g.setColour (colours::text.withAlpha (button.isEnabled() ? 1.0f : 0.5f));
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (button.getButtonText(),
                        bounds.withTrimmedLeft (boxSize + 8.0f),
                        juce::Justification::centredLeft, false);
        }
    };
} // namespace sauce::ui
