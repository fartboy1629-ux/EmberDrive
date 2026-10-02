#include "PluginEditor.h"

//==============================================================================
EmberDriveEditor::EmberDriveEditor (EmberDriveProcessor& p)
    : AudioProcessorEditor (&p),
      driveKnob    (p.apvts, ParamIDs::drive,   "Drive"),
      biasKnob     (p.apvts, ParamIDs::bias,    "Bias", true),
      textureKnob  (p.apvts, ParamIDs::texture, "Texture"),
      toneKnob     (p.apvts, ParamIDs::tone,    "Tone"),
      mixKnob      (p.apvts, ParamIDs::mix,     "Mix"),
      outputKnob   (p.apvts, ParamIDs::output,  "Output"),
      modeSelector (*p.apvts.getParameter (ParamIDs::mode)),
      curve        (p.apvts),
      beforeAfter  (p),
      loudness     (p),
      autoGainAttachment (p.apvts, ParamIDs::autoGain, autoGainButton)
{
    for (auto* c : std::initializer_list<juce::Component*> { &driveKnob, &biasKnob, &textureKnob, &toneKnob,
                                                             &mixKnob, &outputKnob, &modeSelector, &curve,
                                                             &beforeAfter, &loudness, &autoGainButton })
        addAndMakeVisible (c);

    // Set after the children are added so they all pick it up (incl. the knobs' value boxes)
    setLookAndFeel (&lookAndFeel);

    textureKnob.setTooltip ("Analog life: the bias sags on loud hits, each channel drifts slowly, "
                            "and a fine grain rides on the signal. 0% is clean and static.");
    biasKnob.setTooltip ("Pushes the clipper off-centre for more even harmonics");
    autoGainButton.setTooltip ("Keeps the level roughly constant as you turn Drive up");

    setSize (940, 574);
}

EmberDriveEditor::~EmberDriveEditor()
{
    setLookAndFeel (nullptr);
}

void EmberDriveEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (Palette::background.brighter (0.08f), 0.0f, 0.0f,
                                             Palette::background.darker (0.25f), 0.0f, (float) getHeight(), false));
    g.fillAll();

    // Title with a small glowing ember
    auto t = titleArea.toFloat();
    auto titleLine = t.removeFromTop (40.0f);
    const auto dot = titleLine.removeFromLeft (22.0f).withSizeKeepingCentre (12.0f, 12.0f);

    g.setGradientFill (juce::ColourGradient (Palette::accent.withAlpha (0.55f), dot.getCentre(),
                                             Palette::accent.withAlpha (0.0f), dot.getCentre().translated (14.0f, 0.0f), true));
    g.fillEllipse (dot.expanded (8.0f));
    g.setColour (Palette::accent);
    g.fillEllipse (dot);

    juce::AttributedString title;
    title.append ("EMBER ", juce::FontOptions (30.0f, juce::Font::bold), Palette::text);
    title.append ("DRIVE",  juce::FontOptions (30.0f, juce::Font::bold), Palette::accent);
    title.setJustification (juce::Justification::centredLeft);
    title.draw (g, titleLine.withTrimmedLeft (6.0f));

    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::CharPointer_UTF8 ("Analog-style distortion  \xc2\xb7  4\xc3\x97 oversampled"),
                t.withTrimmedLeft (28.0f), juce::Justification::topLeft);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("CHARACTER", modeLabelArea, juce::Justification::bottomLeft);

    // Knob panel
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (knobPanel, 12.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (knobPanel.reduced (0.5f), 12.0f, 1.0f);

    const auto dividerX = (float) driveKnob.getRight() + 6.0f;
    g.drawLine (dividerX, knobPanel.getY() + 20.0f, dividerX, knobPanel.getBottom() - 20.0f, 1.0f);

    // Footer
    g.setColour (Palette::textDim.withAlpha (0.7f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String ("Ember Audio  ") + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  v" + JucePlugin_VersionString,
                footerArea, juce::Justification::centredLeft);
}

void EmberDriveEditor::resized()
{
    auto area = getLocalBounds().reduced (18);

    // Header: title on the left, Character selector on the right
    auto header = area.removeFromTop (70);
    auto selectorArea = header.removeFromRight (420);
    modeSelector.setBounds (selectorArea.removeFromBottom (36));
    modeLabelArea = selectorArea.removeFromBottom (22);
    titleArea = header;

    area.removeFromTop (18);
    footerArea = area.removeFromBottom (26);
    area.removeFromBottom (10);

    // Loudness column on the right, beside both the graphs and the knobs
    loudness.setBounds (area.removeFromRight (128));
    area.removeFromRight (14);

    auto graphs = area.removeFromTop (220);
    curve.setBounds (graphs.removeFromRight (220));
    graphs.removeFromRight (14);
    beforeAfter.setBounds (graphs);

    area.removeFromTop (14);
    knobPanel = area.toFloat();

    auto knobs = area.reduced (14, 12);
    driveKnob.setBounds (knobs.removeFromLeft (170));
    knobs.removeFromLeft (12);

    const int w = knobs.getWidth() / 5;
    for (auto* k : { &biasKnob, &textureKnob, &toneKnob, &mixKnob, &outputKnob })
        k->setBounds (knobs.removeFromLeft (w).withSizeKeepingCentre (w, juce::jmin (knobs.getHeight(), 124)));

    autoGainButton.setBounds (footerArea.getX() + (int) knobPanel.getWidth() - 120, footerArea.getY(), 120, footerArea.getHeight());
}
