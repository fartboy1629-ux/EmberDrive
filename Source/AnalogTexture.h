#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

// The "Texture" control: three small analog behaviours layered on top of the
// clipper. Each is subtle on its own; together they make the distortion feel
// alive instead of static.
//
//  Sag    Loud passages pull the operating point and the gain down a touch, the
//         way a tube stage's bias shifts and its power supply sags. Even
//         harmonics bloom on hard hits and relax as notes decay.
//  Drift  Each channel's drive and bias wander very slowly, like real
//         components that are never quite identical: gentle movement and a
//         little stereo width, even on a mono source.
//  Grain  Band-limited noise that rides on the signal (silent in silence),
//         injected before the clipper so it gets shaped into fine grit rather
//         than sitting on top as hiss.
//
// The random sources are re-seeded on every reset(), so an offline bounce
// sounds exactly like playback.
namespace ember
{

inline constexpr float sagGainDepth    = 0.30f;    // up to about -2 dB of gain sag at full Texture
inline constexpr float sagBiasDepth    = 0.075f;   // operating-point shift, in Bias-knob units
inline constexpr float driftDriveDepth = 0.06f;    // about +/-0.5 dB of slow drive wander
inline constexpr float driftBiasDepth  = 0.04f;    // slow bias wander
inline constexpr float grainDepth      = 0.0035f;  // grain level relative to the signal at full Texture

// A smooth, bounded random wander in [-1, 1]: glides between random targets
// with smoothstep easing, each glide taking 0.4 - 1.2 seconds.
class Drift
{
public:
    void reset() noexcept { from = to = 0.0f; phase = 1.0f; inc = 0.0f; }

    float advance (juce::Random& rng, float stepSeconds) noexcept
    {
        phase += inc;

        if (phase >= 1.0f)
        {
            from  = to;
            to    = rng.nextFloat() * 2.0f - 1.0f;
            phase = 0.0f;
            inc   = stepSeconds / (0.4f + 0.8f * rng.nextFloat());
        }

        const float s = phase * phase * (3.0f - 2.0f * phase);
        return from + (to - from) * s;
    }

private:
    float from = 0.0f, to = 0.0f, phase = 1.0f, inc = 0.0f;
};

struct TextureCoefficients
{
    float grainAttack = 0, grainRelease = 0, sagAttack = 0, sagRelease = 0;
    float noiseLowPass = 0, noiseHighPass = 0, grainNorm = 1;
    float controlStep = 0;     // seconds between control-rate updates
    int   controlInterval = 1; // samples between control-rate updates

    void prepare (double rate)
    {
        auto envelope = [rate] (double seconds) { return (float) (1.0 - std::exp (-1.0 / (seconds * rate))); };
        auto onePole  = [rate] (double hz)      { return (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * hz / rate)); };

        grainAttack   = envelope (0.001);
        grainRelease  = envelope (0.050);
        sagAttack     = envelope (0.004);
        sagRelease    = envelope (0.120);
        noiseLowPass  = onePole (6000.0);
        noiseHighPass = onePole (500.0);

        // Uniform white noise has an RMS of 1/sqrt(3); the one-pole low-pass scales it by
        // sqrt(a / (2 - a)). Undo both so the grain noise has roughly unit RMS.
        grainNorm = (float) (std::sqrt (3.0) / std::sqrt (noiseLowPass / (2.0 - noiseLowPass)));

        controlInterval = juce::jmax (1, (int) (rate / 3000.0));
        controlStep     = (float) (controlInterval / rate);
    }
};

class TextureChannel
{
public:
    void reset (int seed) noexcept
    {
        rng.setSeed (seed);
        driveDrift.reset();
        biasDrift.reset();
        sagEnv = grainEnv = noiseLp = noiseLp500 = 0.0f;
        driveMod = 1.0f;
        biasMod  = 0.0f;
    }

    // Slow drift, updated at about 3 kHz
    void updateControl (const TextureCoefficients& c, float texture) noexcept
    {
        driveMod = std::exp (texture * driftDriveDepth * driveDrift.advance (rng, c.controlStep));
        biasMod  = texture * driftBiasDepth * biasDrift.advance (rng, c.controlStep);
    }

    // Grain to add to the clipper input, following the input's level
    float grain (const TextureCoefficients& c, float x, float texture) noexcept
    {
        const float level = std::abs (x);
        grainEnv += (level - grainEnv) * (level > grainEnv ? c.grainAttack : c.grainRelease);

        const float white = rng.nextFloat() * 2.0f - 1.0f;
        noiseLp    += (white   - noiseLp)    * c.noiseLowPass;    // < 6 kHz
        noiseLp500 += (noiseLp - noiseLp500) * c.noiseHighPass;   // > 500 Hz (subtract the lows)

        return (noiseLp - noiseLp500) * c.grainNorm * grainEnv * texture * grainDepth;
    }

    float sagGain (float texture) const noexcept { return 1.0f / (1.0f + sagGainDepth * texture * sagEnv); }
    float sagBias (float texture) const noexcept { return -sagBiasDepth * texture * sagEnv; }

    // Feed the clipper's output back into the sag envelope
    void track (const TextureCoefficients& c, float y) noexcept
    {
        const float level = std::abs (y);
        sagEnv += (level - sagEnv) * (level > sagEnv ? c.sagAttack : c.sagRelease);
    }

    float driveMod = 1.0f;   // multiplies the drive gain
    float biasMod  = 0.0f;   // added to the bias (Bias-knob units)

private:
    juce::Random rng;
    Drift driveDrift, biasDrift;
    float sagEnv = 0.0f, grainEnv = 0.0f, noiseLp = 0.0f, noiseLp500 = 0.0f;
};

} // namespace ember
