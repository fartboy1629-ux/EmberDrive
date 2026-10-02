#include "Displays.h"

namespace
{
    juce::String dash() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")); }

    void drawHint (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text)
    {
        g.setColour (Palette::textDim.withAlpha (0.7f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (text, area, juce::Justification::centred);
    }
}

//==============================================================================
// Transfer curve
//==============================================================================
CurveDisplay::CurveDisplay (juce::AudioProcessorValueTreeState& state)
    : driveParam (state.getRawParameterValue (ParamIDs::drive)),
      biasParam  (state.getRawParameterValue (ParamIDs::bias)),
      modeParam  (state.getRawParameterValue (ParamIDs::mode))
{
    timerCallback();
    startTimerHz (30);
}

void CurveDisplay::timerCallback()
{
    const float d = driveParam->load();
    const float b = biasParam->load();
    const float m = modeParam->load();

    if (! juce::exactlyEqual (d, lastDrive) || ! juce::exactlyEqual (b, lastBias) || ! juce::exactlyEqual (m, lastMode))
    {
        lastDrive = d;
        lastBias  = b;
        lastMode  = m;
        repaint();
    }
}

void CurveDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("TRANSFER CURVE", bounds.reduced (12.0f, 10.0f).removeFromTop (14.0f), juce::Justification::centredLeft);

    const auto plot = bounds.reduced (14.0f).withTrimmedTop (16.0f);
    constexpr float range = 1.25f;

    auto toScreen = [&] (float x, float y)
    {
        return juce::Point<float> (juce::jmap (x, -1.0f, 1.0f, plot.getX(), plot.getRight()),
                                   juce::jmap (y, -range, range, plot.getBottom(), plot.getY()));
    };

    // Axes, +/-1 guides and the clean (unity) line
    g.setColour (Palette::track);
    g.drawLine ({ toScreen (-1.0f, 0.0f), toScreen (1.0f, 0.0f) }, 1.0f);
    g.drawLine ({ toScreen (0.0f, -range), toScreen (0.0f, range) }, 1.0f);

    const float dashes[] = { 3.0f, 4.0f };
    g.setColour (Palette::track.withAlpha (0.8f));
    g.drawDashedLine ({ toScreen (-1.0f,  1.0f), toScreen (1.0f,  1.0f) }, dashes, 2, 1.0f);
    g.drawDashedLine ({ toScreen (-1.0f, -1.0f), toScreen (1.0f, -1.0f) }, dashes, 2, 1.0f);
    g.drawDashedLine ({ toScreen (-1.0f, -1.0f), toScreen (1.0f,  1.0f) }, dashes, 2, 1.0f);

    // The clipper's raw output, so you can see where it clips on each side and
    // how Bias shifts the operating point away from the centre.
    const auto  mode  = (ember::Mode) juce::jlimit (0, ember::numModes - 1, (int) lastMode);
    const auto  shape = ember::Shapers::get (mode);
    const float gain  = juce::Decibels::decibelsToGain (lastDrive);
    const float bias  = ember::scaledBias (ember::baseBias (mode, lastBias), gain);

    juce::Path p;
    const int steps = juce::jmax (64, (int) plot.getWidth() * 2);

    for (int i = 0; i <= steps; ++i)
    {
        const float x  = -1.0f + 2.0f * (float) i / (float) steps;
        const float y  = juce::jlimit (-range, range, ember::driveStage (shape, x, gain, bias, 0.0f));
        const auto  pt = toScreen (x, y);

        if (i == 0) p.startNewSubPath (pt);
        else        p.lineTo (pt);
    }

    g.setColour (Palette::accent.withAlpha (0.18f));
    g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (Palette::accent);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
// Before / After
//==============================================================================
BeforeAfterDisplay::BeforeAfterDisplay (EmberDriveProcessor& p) : processor (p)
{
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    specIn.fill (dbFloor);
    specOut.fill (dbFloor);
    targetIn.fill (dbFloor);
    targetOut.fill (dbFloor);

    waveButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    spectrumButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    waveButton.setTooltip ("Waveform: how the distortion reshapes the signal");
    spectrumButton.setTooltip ("Spectrum: the harmonics the distortion adds");
    waveButton.onClick     = [this] { setView (View::wave, true); };
    spectrumButton.onClick = [this] { setView (View::spectrum, true); };
    addAndMakeVisible (waveButton);
    addAndMakeVisible (spectrumButton);

    const int stored = processor.apvts.state.getProperty (StateIDs::analyserView, 0);
    setView (stored == 1 ? View::spectrum : View::wave, false);

    startTimerHz (30);
}

void BeforeAfterDisplay::setView (View v, bool storeInState)
{
    view = v;
    waveButton.setToggleState (v == View::wave, juce::dontSendNotification);
    spectrumButton.setToggleState (v == View::spectrum, juce::dontSendNotification);

    if (storeInState)
        processor.apvts.state.setProperty (StateIDs::analyserView, (int) v, nullptr);

    repaint();
}

void BeforeAfterDisplay::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (36);

    auto buttons = headerArea.reduced (10, 8).removeFromRight (152);
    spectrumButton.setBounds (buttons.removeFromRight (80));
    waveButton.setBounds (buttons.removeFromRight (72));

    plotArea = r.reduced (10, 0).withTrimmedBottom (10).toFloat();
}

void BeforeAfterDisplay::timerCallback()
{
    // Follow the stored view (it changes when a session is reloaded)
    const int stored = processor.apvts.state.getProperty (StateIDs::analyserView, (int) view);
    if (stored != (int) view)
        setView (stored == 1 ? View::spectrum : View::wave, false);

    const auto& taps   = processor.getTaps();
    const auto written = taps.written.load (std::memory_order_acquire);
    const bool fresh   = written != lastWritten;
    lastWritten = written;

    const int    latency    = juce::jlimit (0, 1024, taps.latency.load());
    const double sampleRate = juce::jmax (8000.0, taps.sampleRate.load());

    if (view == View::wave)
    {
        if (fresh)
        {
            captureWave (written, latency, sampleRate);
            repaint();
        }
    }
    else
    {
        captureSpectrum (written, latency, sampleRate, fresh);
        repaint();
    }
}

void BeforeAfterDisplay::captureWave (uint32_t written, int latency, double sampleRate)
{
    const int windowLen = juce::jlimit (256, 4096, (int) (sampleRate * 0.02));   // ~20 ms
    const int searchLen = windowLen;
    const int total     = windowLen + searchLen;

    scratchIn.resize ((size_t) total);
    scratchOut.resize ((size_t) total);

    // output[n] belongs with input[n - latency]
    const auto& taps = processor.getTaps();
    taps.input.read  (written - (uint32_t) latency, scratchIn.data(),  total);
    taps.output.read (written,                      scratchOut.data(), total);

    float peakIn = 0.0f, peakOut = 0.0f;
    for (int i = 0; i < total; ++i)
    {
        peakIn  = juce::jmax (peakIn,  std::abs (scratchIn[(size_t) i]));
        peakOut = juce::jmax (peakOut, std::abs (scratchOut[(size_t) i]));
    }

    // Trigger on the latest clean rising zero crossing of the input so that
    // repeating sounds stand still on screen.
    const float hysteresis = peakIn * 0.1f;
    int start = searchLen;

    for (int t = searchLen; t >= 1; --t)
    {
        if (scratchIn[(size_t) t - 1] < 0.0f && scratchIn[(size_t) t] >= 0.0f)
        {
            bool genuine = false;
            for (int j = juce::jmax (0, t - 64); j < t && ! genuine; ++j)
                genuine = scratchIn[(size_t) j] < -hysteresis;

            if (genuine)
            {
                start = t;
                break;
            }
        }
    }

    waveIn.assign  (scratchIn.begin()  + start, scratchIn.begin()  + start + windowLen);
    waveOut.assign (scratchOut.begin() + start, scratchOut.begin() + start + windowLen);
    waveMs = (float) (1000.0 * windowLen / sampleRate);

    float shownPeak = 0.0f;
    for (size_t i = 0; i < waveIn.size(); ++i)
        shownPeak = juce::jmax (shownPeak, std::abs (waveIn[i]), std::abs (waveOut[i]));

    // Auto-zoom: zoom out quickly, zoom in gently, never further than -34 dBFS full scale
    const float targetZoom = 0.9f / juce::jmax (shownPeak, 0.02f);
    waveZoom += (targetZoom - waveZoom) * (targetZoom < waveZoom ? 0.5f : 0.08f);

    signalPresent = juce::jmax (peakIn, peakOut) > 1.0e-4f;
}

void BeforeAfterDisplay::captureSpectrum (uint32_t written, int latency, double sampleRate, bool fresh)
{
    if (fresh)
    {
        scratchIn.resize ((size_t) fftSize);
        scratchOut.resize ((size_t) fftSize);

        const auto& taps = processor.getTaps();
        taps.input.read  (written - (uint32_t) latency, scratchIn.data(),  fftSize);
        taps.output.read (written,                      scratchOut.data(), fftSize);

        analyse (scratchIn.data(),  targetIn,  sampleRate);
        analyse (scratchOut.data(), targetOut, sampleRate);
    }
    else
    {
        // Audio stopped: let the curves fall away
        targetIn.fill (dbFloor);
        targetOut.fill (dbFloor);
    }

    // Fast rise, steady fall
    auto smooth = [] (float& v, float target)
    {
        v = target > v ? v + (target - v) * 0.6f : juce::jmax (target, v - 1.5f);
    };

    for (size_t k = 0; k < (size_t) numPoints; ++k)
    {
        smooth (specIn[k],  targetIn[k]);
        smooth (specOut[k], targetOut[k]);
    }
}

void BeforeAfterDisplay::analyse (const float* samples, std::array<float, numPoints>& result, double sampleRate)
{
    std::copy (samples, samples + fftSize, fftData.begin());
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const double binHz    = sampleRate / fftSize;
    const int    maxBin   = fftSize / 2;
    const float  norm     = 4.0f / (float) fftSize;          // Hann window: a 0 dBFS sine reads 0 dB
    const double ratio    = std::pow ((double) maxFreq / minFreq, 1.0 / (numPoints - 1));
    const double halfStep = std::sqrt (ratio);

    for (int k = 0; k < numPoints; ++k)
    {
        const double f = minFreq * std::pow (ratio, (double) k);

        if (f > sampleRate * 0.49)
        {
            result[(size_t) k] = dbFloor;
            continue;
        }

        const double lo = f / halfStep / binHz, hi = f * halfStep / binHz;
        float mag = 0.0f;

        if (hi - lo < 1.0)
        {
            // Low end: fewer bins than points, interpolate
            const double pos  = juce::jlimit (0.0, (double) maxBin - 1.0, f / binHz);
            const int    i0   = (int) pos;
            const float  frac = (float) (pos - i0);
            mag = fftData[(size_t) i0] + (fftData[(size_t) i0 + 1] - fftData[(size_t) i0]) * frac;
        }
        else
        {
            // High end: many bins per point, keep the strongest so harmonics stay visible
            const int b0 = juce::jlimit (0, maxBin, (int) std::floor (lo));
            const int b1 = juce::jlimit (0, maxBin, (int) std::ceil (hi));
            for (int b = b0; b <= b1; ++b)
                mag = juce::jmax (mag, fftData[(size_t) b]);
        }

        result[(size_t) k] = juce::Decibels::gainToDecibels (mag * norm, dbFloor)
                           + tiltDbPerOctave * (float) std::log2 (f / 1000.0);
    }
}

void BeforeAfterDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    paintHeader (g);

    juce::Path clip;
    clip.addRoundedRectangle (plotArea, 6.0f);
    g.setColour (Palette::plot);
    g.fillPath (clip);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (clip);

    if (view == View::wave) paintWave (g, plotArea);
    else                    paintSpectrum (g, plotArea);
}

void BeforeAfterDisplay::paintHeader (juce::Graphics& g)
{
    auto r = headerArea.toFloat().reduced (12.0f, 0.0f);

    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("BEFORE / AFTER", r.removeFromLeft (98.0f), juce::Justification::centredLeft);
    r.removeFromLeft (10.0f);

    auto legend = [&] (const juce::String& name, juce::Colour colour, bool block, float width)
    {
        auto item   = r.removeFromLeft (width);
        auto swatch = item.removeFromLeft (14.0f).withSizeKeepingCentre (14.0f, block ? 8.0f : 2.5f);
        g.setColour (colour);
        g.fillRoundedRectangle (swatch, block ? 2.0f : 1.0f);
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (name, item.withTrimmedLeft (5.0f), juce::Justification::centredLeft);
    };

    legend ("IN",  Palette::trace.withAlpha (0.75f), false, 42.0f);
    legend ("OUT", Palette::accent,                  false, 50.0f);
    legend (view == View::wave ? "CHANGE" : "ADDED", Palette::accent.withAlpha (0.4f), true, 72.0f);
}

void BeforeAfterDisplay::paintWave (juce::Graphics& g, juce::Rectangle<float> plot)
{
    const float cy    = plot.getCentreY();
    const float halfH = plot.getHeight() * 0.5f - 6.0f;

    // Grid: centre line, quarters, and the 0 dBFS lines when they're in view
    g.setColour (Palette::track.withAlpha (0.25f));
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * (float) i / 4.0f), plot.getY(), plot.getBottom());

    g.setColour (Palette::track.withAlpha (0.7f));
    g.drawHorizontalLine ((int) cy, plot.getX(), plot.getRight());

    if (waveZoom <= 1.05f)
    {
        const float dashes[] = { 3.0f, 4.0f };
        g.setColour (Palette::track.withAlpha (0.8f));
        for (float sign : { -1.0f, 1.0f })
        {
            const float y = cy - sign * waveZoom * halfH;
            g.drawDashedLine ({ plot.getX(), y, plot.getRight(), y }, dashes, 2, 1.0f);
        }
    }

    if (waveIn.size() < 2 || ! signalPresent)
    {
        drawHint (g, plot, "Play some audio to compare the signal before and after");
        return;
    }

    const int n = (int) waveIn.size();
    auto point = [&] (int i, float v)
    {
        return juce::Point<float> (plot.getX() + plot.getWidth() * (float) i / (float) (n - 1),
                                   cy - juce::jlimit (-1.15f, 1.15f, v * waveZoom) * halfH);
    };

    juce::Path in, out, change;
    for (int i = 0; i < n; ++i)
    {
        const auto pi = point (i, waveIn[(size_t) i]);
        const auto po = point (i, waveOut[(size_t) i]);

        if (i == 0) { in.startNewSubPath (pi); out.startNewSubPath (po); change.startNewSubPath (po); }
        else        { in.lineTo (pi);          out.lineTo (po);          change.lineTo (po); }
    }
    for (int i = n - 1; i >= 0; --i)
        change.lineTo (point (i, waveIn[(size_t) i]));
    change.closeSubPath();

    // The area between the two traces is what the distortion changed
    g.setColour (Palette::accent.withAlpha (0.16f));
    g.fillPath (change);

    g.setColour (Palette::trace.withAlpha (0.55f));
    g.strokePath (in, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (Palette::accent.withAlpha (0.2f));
    g.strokePath (out, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (Palette::accent);
    g.strokePath (out, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (Palette::textDim.withAlpha (0.7f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (juce::String (juce::roundToInt (waveMs)) + " ms", plot.reduced (8.0f, 5.0f), juce::Justification::bottomRight);
}

void BeforeAfterDisplay::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> plot)
{
    const float logRange = std::log (maxFreq / minFreq);
    auto xForFreq = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / minFreq) / logRange; };
    auto xForPoint = [&] (int k)  { return plot.getX() + plot.getWidth() * (float) k / (float) (numPoints - 1); };
    auto yForDb   = [&] (float db)
    {
        return juce::jmap (juce::jlimit (dbBottom, dbTop, db), dbBottom, dbTop, plot.getBottom(), plot.getY());
    };

    // Grid
    for (float db = -24.0f; db > dbBottom; db -= 24.0f)
    {
        g.setColour (Palette::track.withAlpha (0.25f));
        g.drawHorizontalLine ((int) yForDb (db), plot.getX(), plot.getRight());
    }

    g.setFont (juce::FontOptions (10.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const bool major = juce::exactlyEqual (f, 100.0f) || juce::exactlyEqual (f, 1000.0f) || juce::exactlyEqual (f, 10000.0f);
        const float x = xForFreq (f);
        g.setColour (Palette::track.withAlpha (major ? 0.55f : 0.25f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());

        if (major)
        {
            g.setColour (Palette::textDim.withAlpha (0.7f));
            g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (x + 4.0f, plot.getBottom() - 16.0f, 32.0f, 12.0f),
                        juce::Justification::centredLeft);
        }
    }

    float loudest = dbFloor;
    for (auto v : specOut) loudest = juce::jmax (loudest, v);
    for (auto v : specIn)  loudest = juce::jmax (loudest, v);

    if (loudest < dbBottom + 3.0f)
    {
        drawHint (g, plot, "Play some audio to compare the spectrum before and after");
        return;
    }

    juce::Path inLine, outLine, inBody, added;
    for (int k = 0; k < numPoints; ++k)
    {
        const float x  = xForPoint (k);
        const float yi = yForDb (specIn[(size_t) k]);
        const float yo = yForDb (specOut[(size_t) k]);
        const float yt = juce::jmin (yi, yo);   // whichever is higher on screen

        if (k == 0)
        {
            inLine.startNewSubPath (x, yi);
            outLine.startNewSubPath (x, yo);
            inBody.startNewSubPath (x, plot.getBottom());
            inBody.lineTo (x, yi);
            added.startNewSubPath (x, yt);
        }
        else
        {
            inLine.lineTo (x, yi);
            outLine.lineTo (x, yo);
            inBody.lineTo (x, yi);
            added.lineTo (x, yt);
        }
    }

    inBody.lineTo (plot.getRight(), plot.getBottom());
    inBody.closeSubPath();

    // "Added" = where the output rises above the input: new harmonics, grain, extra level
    for (int k = numPoints - 1; k >= 0; --k)
        added.lineTo (xForPoint (k), yForDb (specIn[(size_t) k]));
    added.closeSubPath();

    g.setColour (Palette::trace.withAlpha (0.06f));
    g.fillPath (inBody);

    g.setGradientFill (juce::ColourGradient (Palette::accent.withAlpha (0.55f), 0.0f, plot.getY(),
                                             Palette::accent.withAlpha (0.10f), 0.0f, plot.getBottom(), false));
    g.fillPath (added);

    g.setColour (Palette::trace.withAlpha (0.55f));
    g.strokePath (inLine, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (Palette::accent.withAlpha (0.2f));
    g.strokePath (outLine, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (Palette::accent);
    g.strokePath (outLine, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
// Loudness meter
//==============================================================================
const std::array<LoudnessMeterView::Target, 3> LoudnessMeterView::targets {{
    { -14.0f, "-14  STREAMING", "-14 LUFS: Spotify's normalization level, the usual streaming reference" },
    { -16.0f, "-16  APPLE",     "-16 LUFS: the level Apple Music is commonly measured to play at" },
    { -23.0f, "-23  BROADCAST", "-23 LUFS: the EBU R128 broadcast standard" }
}};

LoudnessMeterView::LoudnessMeterView (EmberDriveProcessor& p) : processor (p)
{
    const int n = (int) targets.size();

    for (int i = 0; i < n; ++i)
    {
        auto* b = targetButtons.add (new juce::TextButton (targets[(size_t) i].label));
        b->setConnectedEdges ((i > 0     ? juce::Button::ConnectedOnTop    : 0)
                            | (i < n - 1 ? juce::Button::ConnectedOnBottom : 0));
        b->setTooltip (targets[(size_t) i].description);
        b->onClick = [this, i] { selectTarget (i, true); };
        addAndMakeVisible (b);
    }

    setTooltip ("Short-term loudness (3 s) of the plug-in's output. Glows red above the selected target, "
                "or when the true peak goes over -1 dBTP.");

    selectTarget (processor.apvts.state.getProperty (StateIDs::loudnessTarget, 0), false);
    processor.getMeter().takeTruePeak();   // forget peaks from while the window was closed
    startTimerHz (30);
}

void LoudnessMeterView::selectTarget (int index, bool storeInState)
{
    targetIndex = juce::jlimit (0, (int) targets.size() - 1, index);

    for (int i = 0; i < targetButtons.size(); ++i)
        targetButtons[i]->setToggleState (i == targetIndex, juce::dontSendNotification);

    if (storeInState)
        processor.apvts.state.setProperty (StateIDs::loudnessTarget, targetIndex, nullptr);

    repaint();
}

float LoudnessMeterView::levelToY (float lufs) const noexcept
{
    return juce::jmap (juce::jlimit (scaleBottom, scaleTop, lufs), scaleBottom, scaleTop,
                       barArea.getBottom(), barArea.getY());
}

void LoudnessMeterView::resized()
{
    auto r = getLocalBounds().toFloat().reduced (12.0f, 10.0f);

    titleArea = r.removeFromTop (16.0f);
    r.removeFromTop (4.0f);
    readoutArea = r.removeFromTop (44.0f);

    auto buttons = r.removeFromBottom (66.0f);
    for (auto* b : targetButtons)
        b->setBounds (buttons.removeFromTop (22.0f).toNearestInt());

    r.removeFromBottom (10.0f);
    peakArea = r.removeFromBottom (16.0f);
    r.removeFromBottom (14.0f);
    r.removeFromTop (8.0f);

    // scale labels | bar | target label
    auto meter = r.withSizeKeepingCentre (94.0f, r.getHeight());
    scaleArea = meter.removeFromLeft (28.0f);
    meter.removeFromLeft (6.0f);
    barArea = meter.removeFromLeft (24.0f);
}

void LoudnessMeterView::timerCallback()
{
    // Follow the stored target (it changes when a session is reloaded)
    const int stored = processor.apvts.state.getProperty (StateIDs::loudnessTarget, targetIndex);
    if (stored != targetIndex)
        selectTarget (stored, false);

    auto& meter = processor.getMeter();
    const double now = juce::Time::getMillisecondCounterHiRes();

    const auto updates = meter.getUpdateCount();
    if (updates != lastUpdateCount)
    {
        lastUpdateCount = updates;
        lastUpdateTime  = now;
    }

    // If the host stops processing, don't freeze on the last reading
    const bool live = now - lastUpdateTime < 600.0;
    shortTerm = live ? meter.getShortTerm() : ember::LoudnessMeter::silence;

    // True peak: hold the highest value for 2 s, then let it fall
    const float peak   = meter.takeTruePeak();
    const float peakDb = juce::Decibels::gainToDecibels (peak, ember::LoudnessMeter::silence);

    if (peakDb >= truePeakHold)
    {
        truePeakHold      = peakDb;
        truePeakHoldUntil = now + 2000.0;
    }
    else if (now > truePeakHoldUntil)
    {
        truePeakHold = juce::jmax (peakDb, truePeakHold - 0.75f);
    }

    shown += (shortTerm - shown) * 0.35f;

    loudOver = shortTerm > targets[(size_t) targetIndex].lufs;
    peakOver = truePeakHold > truePeakCeiling;
    glow    += ((loudOver || peakOver ? 1.0f : 0.0f) - glow) * 0.22f;

    repaint();
}

void LoudnessMeterView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (Palette::panelEdge.interpolatedWith (Palette::danger, 0.6f * glow));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("LOUDNESS", titleArea, juce::Justification::centredLeft);

    // Big readout
    auto readout = readoutArea;
    g.setColour (loudOver ? Palette::danger : Palette::text);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText (shortTerm > -70.0f ? juce::String (shortTerm, 1) : dash(),
                readout.removeFromTop (28.0f), juce::Justification::centredLeft);
    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText ("LUFS  SHORT-TERM", readout, juce::Justification::topLeft);

    // Scale
    g.setFont (juce::FontOptions (9.5f));
    for (int db = 0; db >= -36; db -= 6)
    {
        const float y = levelToY ((float) db);
        g.setColour (Palette::textDim.withAlpha (0.75f));
        g.drawText (juce::String (db), juce::Rectangle<float> (scaleArea.getX(), y - 6.0f, scaleArea.getWidth() - 4.0f, 12.0f),
                    juce::Justification::centredRight);
        g.setColour (Palette::track);
        g.drawHorizontalLine ((int) y, scaleArea.getRight() - 2.0f, barArea.getX() - 1.0f);
    }

    // Bar
    const float target  = targets[(size_t) targetIndex].lufs;
    const float targetY = levelToY (target);

    juce::Path track;
    track.addRoundedRectangle (barArea, 4.0f);
    g.setColour (Palette::plot);
    g.fillPath (track);

    if (shown > scaleBottom)
    {
        const auto fill = barArea.withTop (levelToY (shown));

        // Red halo when over the standard
        if (glow > 0.01f)
        {
            juce::DropShadow (Palette::danger.withAlpha (0.9f * glow), 22, {}).drawForRectangle (g, fill.toNearestInt());
            juce::DropShadow (Palette::danger.withAlpha (0.5f * glow), 8,  {}).drawForRectangle (g, fill.toNearestInt());
        }

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (track);

        // Amber up to the target...
        g.setGradientFill (juce::ColourGradient (Palette::accentDim, 0.0f, barArea.getBottom(),
                                                 Palette::accent, 0.0f, targetY, false));
        g.fillRect (fill.withTop (juce::jmax (fill.getY(), targetY)));

        // ...red above it
        if (fill.getY() < targetY)
        {
            g.setColour (Palette::danger);
            g.fillRect (fill.withBottom (targetY));
        }

        // ...and the whole bar flushes red while it's over
        if (glow > 0.01f)
        {
            g.setColour (Palette::danger.withAlpha (0.65f * glow));
            g.fillRect (fill);
        }
    }

    g.setColour (Palette::panelEdge);
    g.strokePath (track, juce::PathStrokeType (1.0f));

    // Target marker
    g.setColour (loudOver ? Palette::danger : Palette::text);
    g.fillRect (juce::Rectangle<float> (barArea.getX() - 3.0f, targetY - 1.0f, barArea.getWidth() + 6.0f, 2.0f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (juce::String (juce::roundToInt (target)),
                juce::Rectangle<float> (barArea.getRight() + 6.0f, targetY - 7.0f, 30.0f, 14.0f),
                juce::Justification::centredLeft);

    // True peak
    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText ("TRUE PEAK", peakArea, juce::Justification::centredLeft);
    g.setColour (peakOver ? Palette::danger : Palette::text);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (truePeakHold > -70.0f ? juce::String (truePeakHold, 1) : dash(), peakArea, juce::Justification::centredRight);
}
