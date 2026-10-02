#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Palette
{
    inline const juce::Colour background { 0xff16120f };
    inline const juce::Colour panel      { 0xff221c17 };
    inline const juce::Colour panelEdge  { 0xff3a2f26 };
    inline const juce::Colour plot       { 0xff1a1511 };   // inside graphs
    inline const juce::Colour track      { 0xff3b322a };
    inline const juce::Colour accent     { 0xffff8a3d };
    inline const juce::Colour accentDim  { 0xff7a3f1a };
    inline const juce::Colour trace      { 0xffd8cbbd };   // the "before" signal
    inline const juce::Colour danger     { 0xffff4636 };   // over the loudness standard
    inline const juce::Colour text       { 0xfff2e6d8 };
    inline const juce::Colour textDim    { 0xffa8998a };
    inline const juce::Colour ink        { 0xff1a120b };   // dark text on accent
}

class EmberLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    EmberLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId,       Palette::text);
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId,  Palette::accentDim);
        setColour (juce::Label::textColourId,               Palette::textDim);
        setColour (juce::Label::textWhenEditingColourId,    Palette::text);
        setColour (juce::TextEditor::textColourId,          Palette::text);
        setColour (juce::TextEditor::backgroundColourId,    Palette::panel);
        setColour (juce::TextEditor::highlightColourId,     Palette::accentDim);
        setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
        setColour (juce::CaretComponent::caretColourId,     Palette::accent);
        setColour (juce::TextButton::textColourOffId,       Palette::textDim);
        setColour (juce::TextButton::textColourOnId,        Palette::ink);
        setColour (juce::TooltipWindow::backgroundColourId, Palette::panel.brighter (0.06f));
        setColour (juce::TooltipWindow::textColourId,       Palette::text);
        setColour (juce::TooltipWindow::outlineColourId,    Palette::panelEdge);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        const auto bounds    = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto radius    = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre    = bounds.getCentre();
        const auto lineW     = juce::jmax (3.0f, radius * 0.085f);
        const auto arcRadius = radius - lineW * 0.5f;
        const auto angle     = startAngle + sliderPos * (endAngle - startAngle);
        const juce::PathStrokeType stroke (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // Track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::track);
        g.strokePath (track, stroke);

        // Value arc: from the start, or from 12 o'clock for bipolar controls
        const bool  bipolar = slider.getProperties()["bipolar"];
        const float from    = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (slider.isEnabled() ? Palette::accent : Palette::textDim);
            g.strokePath (value, stroke);
        }

        // Knob body
        const auto knobR      = arcRadius - lineW * 1.7f;
        const auto knobBounds = juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (knobBounds.translated (0.0f, knobR * 0.06f).expanded (1.5f));

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d332b), centre.x, knobBounds.getY(),
                                                 juce::Colour (0xff1d1814), centre.x, knobBounds.getBottom(), false));
        g.fillEllipse (knobBounds);
        g.setColour (Palette::panelEdge);
        g.drawEllipse (knobBounds, 1.0f);

        // Pointer
        const auto pw = juce::jmax (2.0f, knobR * 0.09f);
        juce::Path pointer;
        pointer.addRoundedRectangle (-pw * 0.5f, -knobR * 0.88f, pw, knobR * 0.42f, pw * 0.5f);
        g.setColour (Palette::text);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
    }

    // Segmented buttons (Character selector, view tabs, loudness targets).
    // Corners are rounded only where a button isn't joined to a neighbour.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        const auto r  = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();

        auto fill = on ? Palette::accent : Palette::panel;
        if (highlighted && ! on) fill = fill.brighter (0.12f);
        if (down)                fill = fill.darker (0.15f);

        const bool left = b.isConnectedOnLeft(), right = b.isConnectedOnRight();
        const bool top  = b.isConnectedOnTop(),  bottom = b.isConnectedOnBottom();
        const float corner = juce::jmin (6.0f, r.getHeight() * 0.3f);

        juce::Path p;
        p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), corner, corner,
                               ! (left || top), ! (right || top), ! (left || bottom), ! (right || bottom));
        g.setColour (fill);
        g.fillPath (p);
        g.setColour (on ? Palette::accent : Palette::panelEdge);
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (15.0f, (float) buttonHeight * 0.45f), juce::Font::bold));
    }

    // Pill switch (Auto Gain)
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        auto r          = b.getLocalBounds().toFloat();
        const auto pill = r.removeFromLeft (36.0f).withSizeKeepingCentre (32.0f, 18.0f);
        const bool on   = b.getToggleState();

        g.setColour (on ? Palette::accent : Palette::track);
        g.fillRoundedRectangle (pill, 9.0f);

        const auto dotX = on ? pill.getRight() - 9.0f : pill.getX() + 9.0f;
        g.setColour (on ? Palette::ink : Palette::textDim);
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ dotX, pill.getCentreY() }));

        g.setColour (highlighted ? Palette::text : Palette::textDim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (8.0f), juce::Justification::centredLeft);
    }
};
