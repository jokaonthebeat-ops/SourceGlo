/*
    FitTabView.h - the FIT tab: how the source's band balance sits against
    the source-type target, at full width. Content region 742 x 263.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class FitTabView : public juce::Component, private juce::ChangeListener
{
public:
    explicit FitTabView (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Fit detail");
        processor.analysisChanged.addChangeListener (this);
    }

    ~FitTabView() override
    {
        processor.analysisChanged.removeChangeListener (this);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& model = processor.getAnalysis();

        g.setFont (Fonts::fieldLabel().withHeight (13.0f));
        g.setColour (tokens::white);
        g.drawText ("BAND BALANCE VS TARGET", 24, 20, 300, 16, juce::Justification::centredLeft);

        if (! model.analyzed)
        {
            g.setFont (Fonts::bodyLabel());
            g.setColour (tokens::muted);
            g.drawText ("Play the source and press Analyze to see how its balance",
                        0, 118, 742, 16, juce::Justification::centred);
            g.drawText ("compares to the target for the selected source type.",
                        0, 136, 742, 16, juce::Justification::centred);
            return;
        }

        drawDeviationChart (g, model);
        drawSummary (g, model);
    }

private:
    static const char* bandName (int b)
    {
        static const char* names[5] = { "SUB", "LOW", "LOW MID", "HIGH MID", "HIGH" };
        return names[b];
    }

    static juce::Colour deviationColour (float dev)
    {
        const float mag = std::abs (dev);
        if (mag <= 2.0f) return tokens::green;
        if (mag <= 5.0f) return tokens::gold;
        return tokens::red;
    }

    void drawDeviationChart (juce::Graphics& g, const AnalysisModel& model)
    {
        // +/-12 dB centred chart across the left ~440 px.
        const juce::Rectangle<float> chart (40.0f, 52.0f, 400.0f, 176.0f);
        const float centreY = chart.getCentreY();
        const float pxPerDb = chart.getHeight() * 0.5f / 12.0f;

        // Scale lines at 0 / +-6 / +-12.
        g.setFont (Fonts::make (9.0f));
        for (int db = -12; db <= 12; db += 6)
        {
            const float y = centreY - (float) db * pxPerDb;
            g.setColour (db == 0 ? tokens::stroke : tokens::strokeSoft);
            g.fillRect (chart.getX(), y, chart.getWidth(), 1.0f);
            g.setColour (tokens::muted);
            g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                        6, (int) y - 5, 30, 10, juce::Justification::centredRight);
        }

        const float slot = chart.getWidth() / 5.0f;
        for (int b = 0; b < 5; ++b)
        {
            const float dev = juce::jlimit (-12.0f, 12.0f, model.bandDeviationDb[b]);
            const float x = chart.getX() + slot * (float) b + slot * 0.5f;
            const float barW = 42.0f;
            const auto colour = deviationColour (model.bandDeviationDb[b]);

            const float top = dev >= 0 ? centreY - dev * pxPerDb : centreY;
            const float height = juce::jmax (2.0f, std::abs (dev) * pxPerDb);
            g.setColour (colour.withAlpha (0.28f));
            g.fillRoundedRectangle (x - barW * 0.5f, top, barW, height, 3.0f);
            g.setColour (colour);
            g.drawRoundedRectangle (x - barW * 0.5f, top, barW, height, 3.0f, 1.2f);

            // Value above/below the bar, band label under the chart.
            g.setFont (Fonts::make (11.0f, false, true));
            const float labelY = dev >= 0 ? top - 15.0f : top + height + 3.0f;
            g.drawText ((dev > 0 ? "+" : "") + juce::String (model.bandDeviationDb[b], 1),
                        (int) (x - 30.0f), (int) labelY, 60, 12, juce::Justification::centred);

            g.setFont (Fonts::make (9.5f).withExtraKerningFactor (0.04f));
            g.setColour (tokens::muted);
            g.drawText (bandName (b), (int) (x - 34.0f), (int) chart.getBottom() + 6,
                        68, 11, juce::Justification::centred);
        }
    }

    void drawSummary (juce::Graphics& g, const AnalysisModel& model)
    {
        const int x = 490;

        g.setFont (Fonts::fieldLabel());
        g.setColour (tokens::text);
        g.drawText ("FIT SCORE", x, 52, 120, 14, juce::Justification::centredLeft);
        g.setFont (Fonts::make (40.0f, false, true));
        g.setColour (tokens::cyan);
        g.drawText (juce::String (model.fit), x, 68, 70, 40, juce::Justification::centredLeft);
        g.setFont (Fonts::make (12.0f));
        g.setColour (tokens::muted);
        g.drawText ("/100", x + 68, 88, 40, 16, juce::Justification::centredLeft);

        // Worst offender callout.
        int worst = 0;
        for (int b = 1; b < 5; ++b)
            if (std::abs (model.bandDeviationDb[b]) > std::abs (model.bandDeviationDb[worst]))
                worst = b;

        g.setFont (Fonts::bodyLabel());
        g.setColour (tokens::text);
        if (std::abs (model.bandDeviationDb[worst]) <= 2.0f)
            g.drawText ("Balance is on target for this source type.",
                        x, 124, 240, 16, juce::Justification::centredLeft);
        else
        {
            const float dev = model.bandDeviationDb[worst];
            g.drawText (juce::String ("Biggest offset: ") + bandName (worst) + " "
                          + (dev > 0 ? "+" : "") + juce::String (dev, 1) + " dB "
                          + (dev > 0 ? "over" : "under") + " target.",
                        x, 124, 250, 16, juce::Justification::centredLeft);
            g.setColour (tokens::muted);
            g.drawText (processor.isFixEngaged()
                          ? "Fix Source is countering this."
                          : "Fix Source will counter this.",
                        x, 142, 250, 16, juce::Justification::centredLeft);
        }

        // Band fit bars, wider than the Analyze tab's.
        for (int b = 0; b < 5; ++b)
        {
            const int y = 172 + b * 18;
            g.setFont (Fonts::make (9.0f));
            g.setColour (tokens::muted);
            g.drawText (bandName (b), x, y, 56, 11, juce::Justification::centredLeft);

            const juce::Rectangle<float> track ((float) x + 62.0f, (float) y + 2.0f, 120.0f, 7.0f);
            g.setColour (tokens::bg1);
            g.fillRoundedRectangle (track, 2.5f);
            g.setColour (tokens::cyanMid);
            g.fillRoundedRectangle (track.withWidth (track.getWidth()
                                       * (float) model.bandFit[b] / 100.0f), 2.5f);

            g.setFont (Fonts::make (10.0f, true));
            g.setColour (tokens::text);
            g.drawText (juce::String (model.bandFit[b]), x + 188, y, 24, 11,
                        juce::Justification::centredRight);
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override  { repaint(); }

    SourceGloProcessor& processor;
};

} // namespace sourceglo
