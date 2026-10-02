#include "Controls.h"

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
            const juce::String& title, bool bipolar)
{
    label.setText (title.toUpperCase(), juce::dontSendNotification);
    label.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, Palette::textDim);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);

    // Double-click a knob to return it to its default
    if (auto* p = state.getParameter (paramID))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (18));
    slider.setBounds (r);
}

//==============================================================================
ModeSelector::ModeSelector (juce::RangedAudioParameter& param)
    : attachment (param, [this] (float v) { showSelection (juce::roundToInt (v)); })
{
    const auto names = param.getAllValueStrings();

    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (names[i].toUpperCase()));
        b->setConnectedEdges ((i > 0                ? juce::Button::ConnectedOnLeft  : 0)
                            | (i < names.size() - 1 ? juce::Button::ConnectedOnRight : 0));
        b->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }

    attachment.sendInitialUpdate();
}

void ModeSelector::showSelection (int index)
{
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == index, juce::dontSendNotification);
}

void ModeSelector::resized()
{
    auto r = getLocalBounds();
    const int n = juce::jmax (1, buttons.size());

    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds (r.removeFromLeft (r.getWidth() / (n - i)));
}
