#pragma once

#include "PluginProcessor.h"
#include "EmberLookAndFeel.h"

//==============================================================================
// Live plot of the current transfer curve (input level -> output level).
class CurveDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit CurveDisplay (juce::AudioProcessorValueTreeState& state);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    std::atomic<float>* driveParam;
    std::atomic<float>* biasParam;
    std::atomic<float>* modeParam;
    float lastDrive = -1.0f, lastBias = -10.0f, lastMode = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveDisplay)
};

//==============================================================================
// The signal before and after the plug-in, overlaid so you can see exactly what
// the distortion changes: as waveforms (shape) or as spectra (added harmonics).
class BeforeAfterDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit BeforeAfterDisplay (EmberDriveProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class View { wave = 0, spectrum = 1 };

    static constexpr int   fftOrder  = 12;
    static constexpr int   fftSize   = 1 << fftOrder;
    static constexpr int   numPoints = 220;
    static constexpr float minFreq   = 20.0f, maxFreq = 20000.0f;
    static constexpr float dbTop     = 6.0f,  dbBottom = -90.0f, dbFloor = -120.0f;
    static constexpr float tiltDbPerOctave = 3.0f;   // makes typical music read flat-ish

    void timerCallback() override;
    void setView (View, bool storeInState);
    void captureWave (uint32_t written, int latency, double sampleRate);
    void captureSpectrum (uint32_t written, int latency, double sampleRate, bool fresh);
    void analyse (const float* samples, std::array<float, numPoints>& result, double sampleRate);

    void paintHeader   (juce::Graphics&);
    void paintWave     (juce::Graphics&, juce::Rectangle<float> plot);
    void paintSpectrum (juce::Graphics&, juce::Rectangle<float> plot);

    EmberDriveProcessor& processor;
    View view = View::wave;
    juce::TextButton waveButton { "WAVE" }, spectrumButton { "SPECTRUM" };

    // Wave view
    std::vector<float> scratchIn, scratchOut, waveIn, waveOut;
    float waveZoom = 1.0f, waveMs = 20.0f;
    bool  signalPresent = false;

    // Spectrum view
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> fftData;
    std::array<float, numPoints> specIn {}, specOut {}, targetIn {}, targetOut {};

    uint32_t lastWritten = 0;
    juce::Rectangle<int> headerArea;
    juce::Rectangle<float> plotArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeforeAfterDisplay)
};

//==============================================================================
// Short-term loudness (LUFS) bar with a selectable industry target. Glows red
// when the short-term loudness is above the target, or the true peak is above
// -1 dBTP.
class LoudnessMeterView final : public juce::Component,
                                public juce::SettableTooltipClient,
                                private juce::Timer
{
public:
    explicit LoudnessMeterView (EmberDriveProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    struct Target { float lufs; const char* label; const char* description; };
    static const std::array<Target, 3> targets;
    static constexpr float truePeakCeiling = -1.0f;   // dBTP: EBU R128 and Spotify's recommendation

private:
    static constexpr float scaleTop = 0.0f, scaleBottom = -42.0f;

    void timerCallback() override;
    void selectTarget (int index, bool storeInState);
    float levelToY (float lufs) const noexcept;

    EmberDriveProcessor& processor;
    juce::OwnedArray<juce::TextButton> targetButtons;
    int targetIndex = 0;

    float shortTerm = ember::LoudnessMeter::silence, shown = ember::LoudnessMeter::silence;
    float truePeakHold = ember::LoudnessMeter::silence;
    double truePeakHoldUntil = 0.0, lastUpdateTime = 0.0;
    uint32_t lastUpdateCount = 0;
    float glow = 0.0f;
    bool loudOver = false, peakOver = false;

    juce::Rectangle<float> titleArea, readoutArea, scaleArea, barArea, peakArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessMeterView)
};
