#pragma once

#include <cmath>

// Static (memoryless) transfer curves that model the clipping stages of
// classic analog circuits. Every curve has unity slope at 0, so a quiet signal
// passes through almost clean and saturation builds up gradually with level,
// the way real circuits behave.
namespace ember
{

enum class Mode
{
    Tube = 0,   // asymmetric soft clip  -> warm, even + odd harmonics
    Tape,       // very soft symmetric   -> smooth compression, mostly odd harmonics
    Diode,      // firm symmetric knee   -> overdrive-pedal crunch
    Fuzz        // hard, lopsided clip   -> buzzy, transistor-fuzz character
};

inline constexpr int   numModes  = 4;
inline constexpr float biasScale = 0.5f;   // Bias knob (-1..1) -> operating-point offset

// Each circuit sits at a slightly different operating point. A tube stage and a
// starved fuzz transistor are biased off-centre, which is where their even
// harmonics come from; tape and a diode pair are symmetric.
inline float innateBias (Mode m) noexcept
{
    switch (m)
    {
        case Mode::Tube: return 0.12f;
        case Mode::Fuzz: return 0.20f;
        case Mode::Tape:
        case Mode::Diode: break;
    }
    return 0.0f;
}

// Operating-point offset for a given Bias knob position (before drive scaling).
inline float baseBias (Mode m, float knob) noexcept { return knob * biasScale + innateBias (m); }

// The offset grows with drive (by its square root) so the asymmetry - and with it
// the even harmonics - survives at high gain instead of vanishing.
inline float scaledBias (float base, float gain) noexcept { return base * std::sqrt (gain); }

using ShaperFn = float (*) (float) noexcept;

struct Shapers
{
    // Algebraic soft clippers: x / (1 + |x|^n)^(1/n). Bigger n = sharper knee.
    static float knee2 (float x) noexcept { return x / std::sqrt (1.0f + x * x); }

    static float knee4 (float x) noexcept
    {
        const float x2 = x * x;
        return x / std::sqrt (std::sqrt (1.0f + x2 * x2));
    }

    static float knee8 (float x) noexcept
    {
        const float x2 = x * x;
        const float x4 = x2 * x2;
        return x / std::sqrt (std::sqrt (std::sqrt (1.0f + x4 * x4)));
    }

    // Triode-like: the positive half rounds off gently, the negative half
    // (grid conduction) clips earlier and harder.
    static float tube (float x) noexcept
    {
        return x >= 0.0f ? std::tanh (x)
                         : 0.7f * knee4 (x / 0.7f);
    }

    static float tape  (float x) noexcept { return knee2 (x); }

    // Back-to-back silicon diodes: a firm knee that levels off a little early.
    static float diode (float x) noexcept { return 0.85f * knee4 (x / 0.85f); }

    // Starved transistor fuzz: positive side clips at 1, negative side at 0.45.
    static float fuzz (float x) noexcept
    {
        return x >= 0.0f ? knee8 (x)
                         : 0.45f * knee8 (x / 0.45f);
    }

    static ShaperFn get (Mode m) noexcept
    {
        switch (m)
        {
            case Mode::Tube:  return &tube;
            case Mode::Tape:  return &tape;
            case Mode::Diode: return &diode;
            case Mode::Fuzz:  return &fuzz;
        }
        return &tube;
    }
};

// Full drive stage: gain -> bias offset -> clipper, re-centred so silence stays silent.
// `offset` must equal shape (bias); it's passed in so the hot loop computes it once per sample.
inline float driveStage (ShaperFn shape, float x, float gain, float bias, float offset) noexcept
{
    // Guard against absurd input levels (keeps the x^8 term finite).
    const float u = std::fmax (-1.0e4f, std::fmin (1.0e4f, x * gain)) + bias;
    return shape (u) - offset;
}

inline float driveStage (ShaperFn shape, float x, float gain, float bias) noexcept
{
    return driveStage (shape, x, gain, bias, shape (bias));
}

// Gain that brings a -12 dBFS sine back to roughly the same peak-to-peak level
// after the drive stage, so turning Drive up changes tone more than loudness.
inline float autoGainFor (ShaperFn shape, float gain, float bias) noexcept
{
    constexpr float ref = 0.25f;
    const float pp = driveStage (shape, ref, gain, bias) - driveStage (shape, -ref, gain, bias);
    return pp > 1.0e-6f ? (2.0f * ref) / pp : 1.0f;
}

} // namespace ember
