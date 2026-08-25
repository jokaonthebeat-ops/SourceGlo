/*
    MaskingFitComponent - radar plot (source vs mix target), fit score and the
    five band-fit bars. Bounds: layout::fitPanel {665,574,339,263}; local.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class MaskingFitComponent : public juce::Component, private juce::Timer,
                            private juce::ChangeListener
{
public:
    explicit MaskingFitComponent (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Masking and fit");

        overlayButton.setToggleState (true, juce::dontSendNotification);
        overlayButton.setRadioGroupId (2);
        deltaButton.setRadioGroupId (2);
        overlayButton.setTooltip ("Overlay source and mix target");
        deltaButton.setTooltip ("Show the difference only");
        addAndMakeVisible (overlayButton);
        addAndMakeVisible (deltaButton);

        processor.analysisChanged.addChangeListener (this);
        startTimerHz (15);
    }

    ~MaskingFitComponent() override
    {
        processor.analysisChanged.removeChangeListener (this);
    }

    void resized() override
    {
        overlayButton.setBounds (219, 22, 58, 20);
        deltaButton.setBounds   (281, 22, 46, 20);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& model = processor.getAnalysis();

        g.setFont (Fonts::fieldLabel().withHeight (13.0f));
        g.setColour (tokens::white);
        g.drawText ("MASKING / FIT", 13, 24, 150, 16, juce::Justification::centredLeft);

        g.setFont (Fonts::make (10.0f));
        g.setColour (tokens::muted);
        g.drawText ("View", 188, 24, 30, 16, juce::Justification::centredLeft);

        drawRadar (g, model);
        drawFitScore (g, model);
        drawBars (g, model);
        drawRadarLegend (g);
    }

private:
    // Radar centre/radius, panel-local (design-global centre ~(800,733)).
    static constexpr float radarCX = 135.0f, radarCY = 159.0f, radarR = 78.0f;

    static juce::Point<float> radarPoint (int axis, float norm)
    {
        // Five axes, SUB at the top, clockwise.
        const float angle = juce::degreesToRadians (-90.0f + 72.0f * (float) axis);
        return { radarCX + norm * radarR * std::cos (angle),
                 radarCY + norm * radarR * std::sin (angle) };
    }

    void drawRadar (juce::Graphics& g, const AnalysisModel& model)
    {
        const juce::Rectangle<float> gridRect (radarCX - radarR - 18.0f, radarCY - radarR - 18.0f,
                                               (radarR + 18.0f) * 2.0f, (radarR + 18.0f) * 2.0f);
        auto grid = Assets::radarGrid();
        if (grid.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (grid, gridRect, juce::RectanglePlacement::centred);
        }

        // Axis labels around the pentagon.
        g.setFont (Fonts::make (9.5f).withExtraKerningFactor (0.04f));
        g.setColour (tokens::muted);
        const char* axisNames[] = { "SUB", "HIGH", "HIGH MID", "LOW MID", "LOW" };
        for (int i = 0; i < 5; ++i)
        {
            const auto pt = radarPoint (i, 1.22f);
            g.drawText (axisNames[i], (int) pt.x - 30, (int) pt.y - 6, 60, 12,
                        juce::Justification::centred);
        }

        // Mix target: dashed gold pentagon.
        {
            juce::Path target;
            for (int i = 0; i <= 5; ++i)
            {
                const auto pt = radarPoint (i % 5, model.radarTarget[i % 5]);
                if (i == 0) target.startNewSubPath (pt);
                else        target.lineTo (pt);
            }
            juce::Path dashed;
            const float dash[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.3f).createDashedStroke (dashed, target, dash, 2);
            g.setColour (tokens::gold);
            g.fillPath (dashed);
        }

        // Source polygon: only once something has been analysed. Gentle
        // breathing keeps the display alive between analyses.
        if (processor.getAnalysis().analyzed)
        {
            juce::Path source;
            for (int i = 0; i <= 5; ++i)
            {
                const float norm = model.radarSource[i % 5] * (1.0f + 0.025f * breathe);
                const auto pt = radarPoint (i % 5, norm);
                if (i == 0) source.startNewSubPath (pt);
                else        source.lineTo (pt);
            }
            g.setColour (tokens::cyan.withAlpha (0.20f));
            g.fillPath (source);
            g.setColour (tokens::cyan);
            g.strokePath (source, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
        }
    }

    void drawFitScore (juce::Graphics& g, const AnalysisModel& model)
    {
        g.setFont (Fonts::fieldLabel());
        g.setColour (tokens::text);
        g.drawText ("FIT SCORE", 240, 66, 90, 14, juce::Justification::centredLeft);

        g.setFont (Fonts::make (34.0f, false, true));
        g.setColour (model.analyzed ? tokens::cyan : tokens::muted);
        g.drawText (model.analyzed ? juce::String (model.fit)
                                   : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93\xe2\x80\x93")),
                    240, 82, 52, 34, juce::Justification::centredLeft);

        g.setFont (Fonts::make (11.0f));
        g.setColour (tokens::muted);
        g.drawText ("/100", 288, 98, 36, 14, juce::Justification::centredLeft);
    }

    void drawBars (juce::Graphics& g, const AnalysisModel& model)
    {
        const char* names[] = { "SUB", "LOW", "LOW MID", "HIGH MID", "HIGH" };
        for (int i = 0; i < 5; ++i)
        {
            const int y = 138 + i * 27;
            g.setFont (Fonts::make (8.5f).withExtraKerningFactor (0.03f));
            g.setColour (tokens::muted);
            g.drawText (names[i], 238, y, 38, 10, juce::Justification::centredLeft);

            const juce::Rectangle<float> track (278.0f, (float) y + 2.0f, 42.0f, 6.0f);
            g.setColour (tokens::bg1);
            g.fillRoundedRectangle (track, 2.0f);
            if (model.analyzed)
            {
                g.setColour (tokens::cyanMid);
                g.fillRoundedRectangle (track.withWidth (track.getWidth()
                                           * (float) model.bandFit[i] / 100.0f), 2.0f);
            }

            g.setFont (Fonts::make (10.0f, true));
            g.setColour (tokens::text);
            g.drawText (model.analyzed ? juce::String (model.bandFit[i])
                                       : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")),
                        322, y - 1, 16, 12, juce::Justification::centredRight);
        }
    }

    void drawRadarLegend (juce::Graphics& g)
    {
        g.setFont (Fonts::make (9.0f));

        g.setColour (tokens::cyan);
        g.fillRect (30, 251, 7, 7);
        g.setColour (tokens::text);
        g.drawText ("Source", 42, 249, 46, 11, juce::Justification::centredLeft);

        g.setColour (tokens::gold);
        g.fillRect (96, 254, 5, 2);
        g.fillRect (103, 254, 5, 2);
        g.drawText ("Mix Target", 113, 249, 64, 11, juce::Justification::centredLeft);
    }

    void timerCallback() override
    {
        if (! isShowing() && ! headlessRefreshMode())
            return;
        breathe = std::sin ((float) (juce::Time::getMillisecondCounter() % 6283) / 1000.0f);
        repaint (20, 45, 215, 220);   // radar area only
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override  { repaint(); }

    SourceGloProcessor& processor;
    SmallPill overlayButton { "Overlay view", "Overlay" };
    SmallPill deltaButton   { "Delta view",   "Delta" };
    float breathe = 0.0f;
};

} // namespace sourceglo
