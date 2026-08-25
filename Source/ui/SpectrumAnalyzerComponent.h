/*
    SpectrumAnalyzerComponent - log-frequency spectrum, 20 Hz..20 kHz,
    +12..-60 dB, cyan source trace, dashed reference, red conflict region.
    Bounds: layout::spectrumPanel {262,574,395,263}; coordinates are local.

    Threading per the implementation spec: the processor's audio thread feeds
    a lock-free FIFO; this component runs the FFT and builds display points on
    the message thread inside its 30 Hz timer; paint() only draws prepared
    points. No allocation after construction.

    UI-milestone behaviour: while no audio has arrived the trace animates from
    deterministic test data shaped like the mockup's kick spectrum, so the
    first-open view matches the approved reference.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"
#include "../dsp/AnalysisEngine.h"

namespace sourceglo
{

class SpectrumAnalyzerComponent : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumAnalyzerComponent (SourceGloProcessor& p)
        : processor (p), fft (SourceGloProcessor::fftOrder)
    {
        setTitle ("Spectrum analyzer");

        preButton.setToggleState (true, juce::dontSendNotification);
        preButton.setRadioGroupId (1);
        postButton.setRadioGroupId (1);
        preButton.setTooltip ("Show the signal before processing");
        postButton.setTooltip ("Show the signal after processing");
        addAndMakeVisible (preButton);
        addAndMakeVisible (postButton);

        fftData.resize (SourceGloProcessor::fftSize * 2, 0.0f);
        window.resize (SourceGloProcessor::fftSize);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (
            window.data(), (size_t) SourceGloProcessor::fftSize,
            juce::dsp::WindowingFunction<float>::hann);

        displayDb.fill (-60.0f);
        smoothedDb.fill (-60.0f);

        startTimerHz (33);
    }

    void resized() override
    {
        preButton.setBounds  (318, 22, 34, 20);
        postButton.setBounds (356, 22, 38, 20);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (Fonts::fieldLabel().withHeight (13.0f));
        g.setColour (tokens::white);
        g.drawText ("SPECTRUM", 12, 24, 150, 16, juce::Justification::centredLeft);

        // Plot area: design-global {299,625,362,210} -> local {37,51}.
        const juce::Rectangle<float> plot (37.0f, 51.0f, 362.0f, 210.0f);

        auto grid = Assets::spectrumGrid();
        if (grid.isValid())
            g.drawImage (grid, plot, juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (tokens::bg1);
            g.fillRect (plot);
        }

        // Axis labels (live text stays sharp; grid art supplies the lines).
        g.setFont (Fonts::make (9.0f));
        g.setColour (tokens::muted);
        const int dbMarks[] = { 12, 0, -12, -24, -36, -48, -60 };
        for (int db : dbMarks)
        {
            const float y = dbToY (plot, (float) db);
            g.drawText (db > 0 ? "+" + juce::String (db) : juce::String (db),
                        2, (int) y - 5, 32, 10, juce::Justification::centredRight);
        }
        const float freqMarks[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        for (float f : freqMarks)
        {
            const float x = hzToX (plot, f);
            g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k"
                                  : juce::String ((int) f),
                        (int) x - 15, (int) plot.getBottom() + 10, 30, 10,
                        juce::Justification::centred);
        }

        drawConflictRegion (g, plot);
        drawReferenceTrace (g, plot);
        drawSourceTrace (g, plot);
        drawLegend (g, plot);
    }

private:
    static constexpr int numBins = 96;

    static float hzToX (juce::Rectangle<float> plot, float hz)
    {
        const float norm = std::log (hz / 20.0f) / std::log (1000.0f);   // 3 decades
        return plot.getX() + norm * plot.getWidth();
    }

    static float dbToY (juce::Rectangle<float> plot, float db)
    {
        return plot.getY() + (12.0f - db) / 72.0f * plot.getHeight();
    }

    static float binHz (int i)
    {
        return 20.0f * std::pow (1000.0f, (float) i / (float) (numBins - 1));
    }

    void drawConflictRegion (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const auto& model = processor.getAnalysis();
        if (model.conflictHiHz <= model.conflictLoHz)
            return;

        const float x1 = hzToX (plot, model.conflictLoHz);
        const float x2 = hzToX (plot, model.conflictHiHz);
        const juce::Rectangle<float> band (x1, plot.getY(), x2 - x1, plot.getHeight());

        auto art = Assets::conflictBand();
        if (art.isValid())
            g.drawImage (art, band, juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (tokens::red.withAlpha (0.14f));
            g.fillRect (band);
        }

        g.setColour (tokens::red.withAlpha (0.75f));
        const float dash[] = { 4.0f, 3.0f };
        g.drawDashedLine ({ { x1, plot.getY() }, { x1, plot.getBottom() } }, dash, 2, 1.0f);
        g.drawDashedLine ({ { x2, plot.getY() }, { x2, plot.getBottom() } }, dash, 2, 1.0f);

        g.setFont (Fonts::make (10.0f, false, true));
        g.setColour (tokens::red);
        g.drawText (model.conflictLabel, (int) x1 + 6, (int) plot.getY() + 8, 160, 12,
                    juce::Justification::centredLeft);
        g.setColour (tokens::gold);
        g.drawText (juce::String ((int) model.conflictLoHz) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))
                      + juce::String ((int) model.conflictHiHz) + " Hz",
                    (int) x1 + 6, (int) plot.getY() + 22, 120, 12,
                    juce::Justification::centredLeft);
    }

    void drawSourceTrace (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        juce::Path trace;
        for (int i = 0; i < numBins; ++i)
        {
            const float x = hzToX (plot, binHz (i));
            const float y = dbToY (plot, smoothedDb[(size_t) i]);
            if (i == 0) trace.startNewSubPath (x, y);
            else        trace.lineTo (x, y);
        }

        // Fill under the trace, then the line.
        juce::Path fill (trace);
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (tokens::cyan.withAlpha (0.30f),
                                                 plot.getX(), plot.getY(),
                                                 tokens::cyan.withAlpha (0.02f),
                                                 plot.getX(), plot.getBottom(), false));
        g.fillPath (fill);

        g.setColour (tokens::cyan);
        g.strokePath (trace, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
    }

    void drawReferenceTrace (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        // The dashed reference is the target curve for the selected source
        // type, drawn with its loudest point at -14 dB display level.
        const int type = (int) processor.getAPVTS().getRawParameterValue (pid::sourceType)->load();

        juce::Path ref;
        for (int i = 0; i < numBins; ++i)
        {
            const float x = hzToX (plot, binHz (i));
            const float y = dbToY (plot, juce::jlimit (-58.0f, 12.0f,
                                -14.0f + AnalysisEngine::targetCurveDb (type, binHz (i))));
            if (i == 0) ref.startNewSubPath (x, y);
            else        ref.lineTo (x, y);
        }
        g.setColour (tokens::muted.withAlpha (0.7f));
        juce::Path dashed;
        const float dash[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, ref, dash, 2);
        g.fillPath (dashed);
    }

    void drawLegend (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const int x = (int) plot.getRight() - 92, y = (int) plot.getY() + 8;
        g.setFont (Fonts::make (9.5f));

        g.setColour (tokens::cyan);
        g.fillRect (x, y + 4, 14, 2);
        g.setColour (tokens::text);
        g.drawText ("SOURCE", x + 20, y, 70, 10, juce::Justification::centredLeft);

        g.setColour (tokens::muted);
        g.fillRect (x, y + 18, 5, 2);
        g.fillRect (x + 7, y + 18, 5, 2);
        g.drawText ("REFERENCE", x + 20, y + 14, 70, 10, juce::Justification::centredLeft);
    }

    void timerCallback() override
    {
        if (! isShowing() && ! headlessRefreshMode())
            return;

        bool gotAudio = false;
        while (processor.pullFFTBlock (fftData.data()))
            gotAudio = true;

        if (gotAudio)
        {
            lastAudioTime = juce::Time::getMillisecondCounter();

            juce::FloatVectorOperations::multiply (fftData.data(), window.data(),
                                                   SourceGloProcessor::fftSize);
            std::fill (fftData.begin() + SourceGloProcessor::fftSize, fftData.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (fftData.data());

            const double sr = processor.getSampleRateHz();
            for (int i = 0; i < numBins; ++i)
            {
                const float hz  = binHz (i);
                const int   bin = juce::jlimit (1, SourceGloProcessor::fftSize / 2 - 1,
                                    (int) std::round (hz / (sr / SourceGloProcessor::fftSize)));
                const float mag = fftData[(size_t) bin] / (float) (SourceGloProcessor::fftSize / 4);
                displayDb[(size_t) i] = juce::Decibels::gainToDecibels (mag, -60.0f);
            }
        }
        else if (juce::Time::getMillisecondCounter() - lastAudioTime > 600)
        {
            // Audio stopped: sink the trace to the floor (the release
            // ballistics below make it a fall, not a cut).
            displayDb.fill (-60.0f);
        }

        // Display ballistics: fast up, slow down.
        bool changed = false;
        for (size_t i = 0; i < (size_t) numBins; ++i)
        {
            const float target = displayDb[i];
            float& s = smoothedDb[i];
            const float next = target > s ? s + (target - s) * 0.55f
                                          : s + (target - s) * 0.18f;
            if (std::abs (next - s) > 0.01f) { s = next; changed = true; }
        }

        if (changed)
            repaint (30, 45, 380, 230);
    }

    SourceGloProcessor& processor;
    juce::dsp::FFT fft;
    std::vector<float> fftData, window;
    std::array<float, numBins> displayDb, smoothedDb;
    juce::uint32 lastAudioTime = 0;

    SmallPill preButton  { "Pre",  "Pre" };
    SmallPill postButton { "Post", "Post" };
};

} // namespace sourceglo
