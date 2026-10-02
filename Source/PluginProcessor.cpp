#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout EmberDriveProcessor::createParameterLayout()
{
    using namespace juce;

    auto dbText   = [] (float v, int) { return String (v, 1) + " dB"; };
    auto pctText  = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto biasText = [] (float v, int)
    {
        const auto pct = roundToInt (v * 100.0f);
        return (pct > 0 ? "+" : "") + String (pct) + " %";
    };
    auto biasParse = [] (const String& t) { return t.getFloatValue() / 100.0f; };
    auto hzText    = [] (float v, int)
    {
        return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz"
                            : String (roundToInt (v)) + " Hz";
    };
    auto hzParse = [] (const String& t)
    {
        const auto v = t.getFloatValue();
        return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
    };

    NormalisableRange<float> toneRange (800.0f, 20000.0f, 1.0f);
    toneRange.setSkewForCentre (4000.0f);

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::drive, 1 }, "Drive",
        NormalisableRange<float> (0.0f, 48.0f, 0.1f), 18.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::mode, 1 }, "Character",
        StringArray { "Tube", "Tape", "Diode", "Fuzz" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::bias, 1 }, "Bias",
        NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (biasText)
                                       .withValueFromStringFunction (biasParse)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::texture, 2 }, "Texture",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 35.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::tone, 1 }, "Tone",
        toneRange, 12000.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (hzText)
                                       .withValueFromStringFunction (hzParse)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::autoGain, 1 }, "Auto Gain", true));

    return layout;
}

//==============================================================================
EmberDriveProcessor::EmberDriveProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EmberDrive", createParameterLayout())
{
    driveParam    = apvts.getRawParameterValue (ParamIDs::drive);
    modeParam     = apvts.getRawParameterValue (ParamIDs::mode);
    biasParam     = apvts.getRawParameterValue (ParamIDs::bias);
    textureParam  = apvts.getRawParameterValue (ParamIDs::texture);
    toneParam     = apvts.getRawParameterValue (ParamIDs::tone);
    mixParam      = apvts.getRawParameterValue (ParamIDs::mix);
    outputParam   = apvts.getRawParameterValue (ParamIDs::output);
    autoGainParam = apvts.getRawParameterValue (ParamIDs::autoGain);
}

//==============================================================================
void EmberDriveProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxChunkSize      = juce::jmax (1, samplesPerBlock);
    preparedChannels  = (size_t) juce::jlimit (1, maxChannels, getTotalNumOutputChannels());

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        preparedChannels, oversamplingOrder,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,    // max quality filters
        true);   // integer latency, so the host can compensate exactly
    oversampler->initProcessing ((size_t) maxChunkSize);

    const auto latency = juce::jlimit (0, maxLatency, (int) std::lround (oversampler->getLatencyInSamples()));
    setLatencySamples (latency);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxChunkSize, (juce::uint32) preparedChannels };

    dryWet.prepare (spec);
    dryWet.setWetLatency ((float) latency);   // keeps dry and wet time-aligned for the Mix knob

    dcBlocker.prepare (spec);
    dcBlocker.setType (juce::dsp::FirstOrderTPTFilterType::highpass);
    dcBlocker.setCutoffFrequency (10.0f);

    toneFilter.prepare (spec);
    toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    toneFilter.setResonance (1.0f / juce::MathConstants<float>::sqrt2);

    const auto osRate = sampleRate * (double) oversampler->getOversamplingFactor();
    driveGain.reset  (osRate, 0.05);
    bias.reset       (osRate, 0.05);
    texture.reset    (osRate, 0.05);
    toneCutoff.reset (sampleRate, 0.05);
    outputGain.reset (sampleRate, 0.05);

    textureCoeffs.prepare (osRate);
    meter.prepare (sampleRate, maxChunkSize, (int) preparedChannels);

    taps.latency.store (latency);
    taps.sampleRate.store (sampleRate);

    updateTargets (true);
    reset();
}

void EmberDriveProcessor::reset()
{
    if (oversampler != nullptr)
        oversampler->reset();

    dryWet.reset();
    dcBlocker.reset();
    toneFilter.reset();
    meter.reset();
    resetTexture();
}

void EmberDriveProcessor::resetTexture() noexcept
{
    // Fixed seeds: every playback (and every offline bounce) gets the same texture
    for (int ch = 0; ch < maxChannels; ++ch)
        textureChannels[(size_t) ch].reset (0x5eed + 7919 * ch);

    controlCountdown = 0;
}

bool EmberDriveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void EmberDriveProcessor::updateTargets (bool snapImmediately)
{
    currentMode = (ember::Mode) juce::jlimit (0, ember::numModes - 1, (int) modeParam->load());

    const float drive  = juce::Decibels::decibelsToGain (driveParam->load());
    const float base   = ember::baseBias (currentMode, biasParam->load());
    const float tex    = textureParam->load() * 0.01f;
    const float cutoff = juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), toneParam->load());

    float out = juce::Decibels::decibelsToGain (outputParam->load());
    if (autoGainParam->load() > 0.5f)
        out *= ember::autoGainFor (ember::Shapers::get (currentMode), drive, ember::scaledBias (base, drive));

    if (snapImmediately)
    {
        driveGain.setCurrentAndTargetValue  (drive);
        bias.setCurrentAndTargetValue       (base);
        texture.setCurrentAndTargetValue    (tex);
        toneCutoff.setCurrentAndTargetValue (cutoff);
        outputGain.setCurrentAndTargetValue (out);
        toneFilter.setCutoffFrequency (cutoff);
    }
    else
    {
        driveGain.setTargetValue  (drive);
        bias.setTargetValue       (base);
        texture.setTargetValue    (tex);
        toneCutoff.setTargetValue (cutoff);
        outputGain.setTargetValue (out);
    }

    dryWet.setWetMixProportion (mixParam->load() * 0.01f);
}

void EmberDriveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (oversampler == nullptr || buffer.getNumSamples() == 0)
        return;

    updateTargets (false);

    const auto numChannels = juce::jmin ((size_t) buffer.getNumChannels(), (size_t) numOut, preparedChannels);
    if (numChannels == 0)
        return;

    auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, numChannels);

    // Some hosts send blocks larger than announced: process in safe-sized chunks.
    const auto total = block.getNumSamples();
    for (size_t pos = 0; pos < total; pos += (size_t) maxChunkSize)
        processChunk (block.getSubBlock (pos, juce::jmin ((size_t) maxChunkSize, total - pos)));

    dcBlocker.snapToZero();
    toneFilter.snapToZero();
}

void EmberDriveProcessor::processChunk (juce::dsp::AudioBlock<float> block)
{
    const auto numChannels = block.getNumChannels();
    const auto numSamples  = block.getNumSamples();

    // Record the channel average for the editor's Before/After display
    const auto  first        = taps.written.load (std::memory_order_relaxed);   // only this thread writes it
    const float channelScale = 1.0f / (float) numChannels;

    auto record = [&] (ember::SampleHistory& history)
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (size_t ch = 0; ch < numChannels; ++ch)
                sum += block.getChannelPointer (ch)[i];

            history.write (first + (uint32_t) i, sum * channelScale);
        }
    };

    record (taps.input);
    dryWet.pushDrySamples (block);

    // 1) Clipping stage, run at 4x the sample rate so the new harmonics don't alias.
    auto os = oversampler->processSamplesUp (block);
    const auto shape = ember::Shapers::get (currentMode);

    for (size_t i = 0; i < os.getNumSamples(); ++i)
    {
        const float g    = driveGain.getNextValue();
        const float base = bias.getNextValue();
        const float tex  = texture.getNextValue();

        if (--controlCountdown <= 0)
        {
            controlCountdown = textureCoeffs.controlInterval;
            for (size_t ch = 0; ch < numChannels; ++ch)
                textureChannels[ch].updateControl (textureCoeffs, tex);
        }

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto& tc = textureChannels[ch];
            auto* d  = os.getChannelPointer (ch);

            // Texture: grain rides on the input, sag and drift move the operating point
            const float x     = d[i] + tc.grain (textureCoeffs, d[i], tex);
            const float b     = ember::scaledBias (base + tc.biasMod + tc.sagBias (tex), g);
            const float gain  = g * tc.driveMod * tc.sagGain (tex);
            const float y     = ember::driveStage (shape, x, gain, b, shape (b));

            tc.track (textureCoeffs, y);
            d[i] = y;
        }
    }

    oversampler->processSamplesDown (block);

    // 2) Remove DC left by asymmetric clipping, tame the top end, set the level.
    const bool toneMoving = toneCutoff.isSmoothing();

    for (size_t i = 0; i < numSamples; ++i)
    {
        if (toneMoving)
            toneFilter.setCutoffFrequency (toneCutoff.getNextValue());

        const float gain = outputGain.getNextValue();

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* d = block.getChannelPointer (ch);
            float x = dcBlocker.processSample ((int) ch, d[i]);
            x = toneFilter.processSample ((int) ch, x);
            d[i] = x * gain;
        }
    }

    // 3) Blend with the (latency-matched) dry signal.
    dryWet.mixWetSamples (block);

    // 4) Measure what leaves the plug-in.
    meter.process (block);
    record (taps.output);
    taps.written.store (first + (uint32_t) numSamples, std::memory_order_release);
}

//==============================================================================
juce::AudioProcessorEditor* EmberDriveProcessor::createEditor()
{
    return new EmberDriveEditor (*this);
}

void EmberDriveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void EmberDriveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EmberDriveProcessor();
}
