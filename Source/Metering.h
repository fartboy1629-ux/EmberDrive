#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace ember
{

//==============================================================================
// Recent samples, written by the audio thread and read by the editor without
// locks. The buffer is far longer than anything the editor reads, so the writer
// never catches up with a reader.
class SampleHistory
{
public:
    static constexpr uint32_t capacity = 1u << 15;   // 32768 samples

    SampleHistory()
    {
        for (auto& s : data)
            s.store (0.0f, std::memory_order_relaxed);
    }

    void write (uint32_t index, float value) noexcept
    {
        data[index & mask].store (value, std::memory_order_relaxed);
    }

    // Copies `num` samples ending just before `endIndex`
    void read (uint32_t endIndex, float* dest, int num) const noexcept
    {
        const uint32_t start = endIndex - (uint32_t) num;
        for (int i = 0; i < num; ++i)
            dest[i] = data[(start + (uint32_t) i) & mask].load (std::memory_order_relaxed);
    }

private:
    static constexpr uint32_t mask = capacity - 1;
    std::array<std::atomic<float>, capacity> data;
};

// What the editor's Before/After display reads
struct AnalysisTaps
{
    SampleHistory input, output;            // channel average, before and after the plug-in
    std::atomic<uint32_t> written { 0 };    // total samples recorded so far
    std::atomic<int>      latency { 0 };    // output[n] lines up with input[n - latency]
    std::atomic<double>   sampleRate { 44100.0 };
};

//==============================================================================
// ITU-R BS.1770 / EBU R128 loudness: momentary (400 ms) and short-term (3 s)
// LUFS, plus 4x-oversampled true peak.
class LoudnessMeter
{
public:
    static constexpr float silence = -100.0f;

    void prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        channels        = juce::jlimit (1, 2, numChannels);
        samplesPer100ms = juce::jmax (1, (int) std::lround (sampleRate * 0.1));
        computeKWeighting (sampleRate);

        truePeakUpsampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
        truePeakUpsampler->initProcessing ((size_t) juce::jmax (1, maxBlockSize));

        reset();
    }

    void reset() noexcept
    {
        stage1.fill ({});
        stage2.fill ({});
        blocks.fill (0.0);
        sum = 0.0;
        count = 0;
        next = 0;

        if (truePeakUpsampler != nullptr)
            truePeakUpsampler->reset();

        momentary.store (silence);
        shortTerm.store (silence);
        truePeak.store (0.0f);
    }

    void process (const juce::dsp::AudioBlock<const float>& block) noexcept
    {
        if (truePeakUpsampler == nullptr)
            return;

        const int numCh = juce::jmin ((int) block.getNumChannels(), channels);
        const int n     = (int) block.getNumSamples();

        for (int i = 0; i < n; ++i)
        {
            double meanSquare = 0.0;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const double y = kWeight (ch, (double) block.getSample (ch, i));
                meanSquare += y * y;
            }

            sum += meanSquare;

            if (++count >= samplesPer100ms)
            {
                pushBlock (sum / count);
                sum = 0.0;
                count = 0;
            }
        }

        // True peak: the highest point of the waveform between the samples too
        auto up = truePeakUpsampler->processSamplesUp (block.getSubsetChannelBlock (0, (size_t) numCh));
        float peak = 0.0f;

        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (up.getChannelPointer (ch), (int) up.getNumSamples());
            peak = juce::jmax (peak, -range.getStart(), range.getEnd());
        }

        float previous = truePeak.load (std::memory_order_relaxed);
        while (peak > previous && ! truePeak.compare_exchange_weak (previous, peak, std::memory_order_relaxed)) {}
    }

    float getMomentary() const noexcept      { return momentary.load (std::memory_order_relaxed); }
    float getShortTerm() const noexcept      { return shortTerm.load (std::memory_order_relaxed); }
    uint32_t getUpdateCount() const noexcept { return updates.load (std::memory_order_relaxed); }

    // Highest true peak (linear) since the last call
    float takeTruePeak() noexcept            { return truePeak.exchange (0.0f, std::memory_order_relaxed); }

private:
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State  { double z1 = 0, z2 = 0; };

    static double run (const Biquad& f, State& s, double x) noexcept
    {
        const double y = f.b0 * x + s.z1;
        s.z1 = f.b1 * x - f.a1 * y + s.z2;
        s.z2 = f.b2 * x - f.a2 * y;
        return y;
    }

    double kWeight (int ch, double x) noexcept
    {
        return run (highPass, stage2[(size_t) ch], run (shelf, stage1[(size_t) ch], x));
    }

    // The two K-weighting stages, derived for any sample rate from the analog
    // prototypes behind the 48 kHz coefficients published in BS.1770.
    void computeKWeighting (double fs)
    {
        const double pi = juce::MathConstants<double>::pi;
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K  = std::tan (pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                      2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K  = std::tan (pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            highPass = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
    }

    void pushBlock (double meanSquare) noexcept
    {
        blocks[(size_t) next] = meanSquare;
        next = (next + 1) % numBlocks;

        double m = 0.0, s = 0.0;
        for (int k = 0; k < numBlocks; ++k)
        {
            const double v = blocks[(size_t) ((next - 1 - k + 2 * numBlocks) % numBlocks)];
            s += v;
            if (k < 4) m += v;
        }

        momentary.store (toLufs (m / 4.0));
        shortTerm.store (toLufs (s / numBlocks));
        updates.fetch_add (1, std::memory_order_relaxed);
    }

    static float toLufs (double meanSquare) noexcept
    {
        return meanSquare > 1.0e-10 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : silence;
    }

    static constexpr int numBlocks = 30;    // 30 x 100 ms = the 3 s short-term window

    Biquad shelf, highPass;
    std::array<State, 2> stage1 {}, stage2 {};
    std::array<double, numBlocks> blocks {};
    double sum = 0.0;
    int count = 0, next = 0, channels = 2, samplesPer100ms = 4800;

    std::unique_ptr<juce::dsp::Oversampling<float>> truePeakUpsampler;

    std::atomic<float>    momentary { silence }, shortTerm { silence }, truePeak { 0.0f };
    std::atomic<uint32_t> updates { 0 };
};

} // namespace ember
