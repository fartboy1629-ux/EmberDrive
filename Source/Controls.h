#pragma once

#include "PluginProcessor.h"
#include "EmberLookAndFeel.h"

//==============================================================================
// Rotary knob with a title above and the value underneath.
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
          const juce::String& title, bool bipolar = false);

    void setTooltip (const juce::String& text) { slider.setTooltip (text); }
    void resized() override;

private:
    juce::Label  label;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

//==============================================================================
// Segmented buttons bound to the "Character" choice parameter.
class ModeSelector final : public juce::Component
{
public:
    explicit ModeSelector (juce::RangedAudioParameter& param);

    void resized() override;

private:
    void showSelection (int index);

    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeSelector)
};
