#pragma once

#include "PluginProcessor.h"
#include "EmberLookAndFeel.h"
#include "Controls.h"
#include "Displays.h"

class EmberDriveEditor final : public juce::AudioProcessorEditor
{
public:
    explicit EmberDriveEditor (EmberDriveProcessor&);
    ~EmberDriveEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    EmberLookAndFeel lookAndFeel;   // declared first so it outlives every component

    Knob driveKnob, biasKnob, textureKnob, toneKnob, mixKnob, outputKnob;
    ModeSelector modeSelector;

    CurveDisplay       curve;
    BeforeAfterDisplay beforeAfter;
    LoudnessMeterView  loudness;

    juce::ToggleButton autoGainButton { "AUTO GAIN" };
    juce::AudioProcessorValueTreeState::ButtonAttachment autoGainAttachment;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::Rectangle<int> titleArea, modeLabelArea, footerArea;
    juce::Rectangle<float> knobPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberDriveEditor)
};
