#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "DistortionEngine.h"
#include "AnalogTexture.h"
#include "Metering.h"

namespace ParamIDs
{
    inline constexpr auto drive    = "drive";
    inline constexpr auto mode     = "mode";
    inline constexpr auto bias     = "bias";
    inline constexpr auto texture  = "texture";
    inline constexpr auto tone     = "tone";
    inline constexpr auto mix      = "mix";
    inline constexpr auto output   = "output";
    inline constexpr auto autoGain = "autogain";
}

// Editor-only settings stored with the plug-in's state (not automatable)
namespace StateIDs
{
    inline constexpr auto loudnessTarget = "loudnessTarget";   // index into the meter's target list
    inline constexpr auto analyserView   = "analyserView";     // 0 = wave, 1 = spectrum
}

class EmberDriveProcessor final : public juce::AudioProcessor
{
public:
    EmberDriveProcessor();
    ~EmberDriveProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor's displays
    const ember::AnalysisTaps& getTaps() const noexcept { return taps; }
    ember::LoudnessMeter& getMeter() noexcept           { return meter; }

private:
    void processChunk (juce::dsp::AudioBlock<float> block);
    void updateTargets (bool snapImmediately);
    void resetTexture() noexcept;

    static constexpr size_t oversamplingOrder = 2;   // 2^2 = 4x oversampling
    static constexpr int    maxLatency        = 256;
    static constexpr int    maxChannels       = 2;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::dsp::DryWetMixer<float> dryWet { maxLatency };
    juce::dsp::FirstOrderTPTFilter<float> dcBlocker;
    juce::dsp::StateVariableTPTFilter<float> toneFilter;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> driveGain, toneCutoff;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         bias, texture, outputGain;

    ember::TextureCoefficients textureCoeffs;
    std::array<ember::TextureChannel, maxChannels> textureChannels;
    int controlCountdown = 0;

    ember::AnalysisTaps  taps;
    ember::LoudnessMeter meter;

    std::atomic<float>* driveParam    = nullptr;
    std::atomic<float>* modeParam     = nullptr;
    std::atomic<float>* biasParam     = nullptr;
    std::atomic<float>* textureParam  = nullptr;
    std::atomic<float>* toneParam     = nullptr;
    std::atomic<float>* mixParam      = nullptr;
    std::atomic<float>* outputParam   = nullptr;
    std::atomic<float>* autoGainParam = nullptr;

    double currentSampleRate = 44100.0;
    int    maxChunkSize      = 0;
    size_t preparedChannels  = 0;
    ember::Mode currentMode  = ember::Mode::Tube;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberDriveProcessor)
};
