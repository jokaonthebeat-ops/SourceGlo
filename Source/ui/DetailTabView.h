/*
    DetailTabView.h - the DETAIL tab: every measured number on one page.
    Content region 742 x 263.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class DetailTabView : public juce::Component, private juce::ChangeListener
{
public:
    explicit DetailTabView (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Analysis detail");
        processor.analysisChanged.addChangeListener (this);
    }

    ~DetailTabView() override
    {
        processor.analysisChanged.removeChangeListener (this);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& model = processor.getAnalysis();

        g.setFont (Fonts::fieldLabel().withHeight (13.0f));
        g.setColour (tokens::white);
        g.drawText ("ANALYSIS DETAIL", 24, 20, 300, 16, juce::Justification::centredLeft);

        if (! model.analyzed)
        {
            g.setFont (Fonts::bodyLabel());
            g.setColour (tokens::muted);
            g.drawText ("Press Analyze while the source plays for the full readout.",
                        0, 126, 742, 16, juce::Justification::centred);
            return;
        }

        drawMeasurements (g, model);
        drawBandTable (g, model);
        drawScoreBreakdown (g, model);
    }

private:
    void row (juce::Graphics& g, int x, int& y, const juce::String& label,
              const juce::String& value, juce::Colour valueColour = tokens::white)
    {
        g.setFont (Fonts::bodyLabel().withHeight (12.0f));
        g.setColour (tokens::bodyLabel);
        g.drawText (label, x, y, 104, 14, juce::Justification::centredLeft);
        g.setFont (Fonts::bodyValue().withHeight (12.0f));
        g.setColour (valueColour);
        g.drawText (value, x + 104, y, 96, 14, juce::Justification::centredRight);
        y += 19;
    }

    void drawMeasurements (juce::Graphics& g, const AnalysisModel& model)
    {
        const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
        const auto& s = model.stats;
        int y = 52;

        g.setFont (Fonts::make (10.0f, false, true).withExtraKerningFactor (0.05f));
        g.setColour (tokens::muted);
        g.drawText ("MEASUREMENTS", 24, y - 16, 200, 12, juce::Justification::centredLeft);

        row (g, 24, y, "Duration", juce::String (s.durationSec, 2) + " s");
        row (g, 24, y, "Tempo", s.tempoBpm > 0 ? juce::String (s.tempoBpm, 1) + " BPM" : dash);
        row (g, 24, y, "Key", s.key.isNotEmpty() ? s.key : dash);
        row (g, 24, y, "Correlation", juce::String (model.correlation, 2),
             model.correlation < 0.15f ? tokens::red : tokens::white);
        row (g, 24, y, "Low correlation", juce::String (model.lowCorrelation, 2),
             model.lowCorrelation < 0.5f ? tokens::amber : tokens::white);
        row (g, 24, y, "Fix engaged", processor.isFixEngaged() ? "Yes" : "No",
             processor.isFixEngaged() ? tokens::gold : tokens::muted);
        row (g, 24, y, "Diagnostics", juce::String ((int) model.diagnostics.size()));
    }

    void drawBandTable (juce::Graphics& g, const AnalysisModel& model)
    {
        const int x = 268, colW = 62;
        static const char* names[5] = { "Sub", "Low", "Low Mid", "High Mid", "High" };
        const int type = (int) processor.getAPVTS().getRawParameterValue (pid::sourceType)->load();

        g.setFont (Fonts::make (10.0f, false, true).withExtraKerningFactor (0.05f));
        g.setColour (tokens::muted);
        g.drawText ("BAND LEVELS (dB RE LOUDEST)", x, 36, 260, 12, juce::Justification::centredLeft);

        g.setFont (Fonts::make (10.0f));
        g.drawText ("Band",   x, 56, 66, 12, juce::Justification::centredLeft);
        g.drawText ("Level",  x + 70, 56, colW, 12, juce::Justification::centredRight);
        g.drawText ("Target", x + 70 + colW, 56, colW, 12, juce::Justification::centredRight);
        g.drawText ("Offset", x + 70 + colW * 2, 56, colW, 12, juce::Justification::centredRight);

        g.setColour (tokens::strokeSoft);
        g.fillRect (x, 71, 70 + colW * 3, 1);

        for (int b = 0; b < 5; ++b)
        {
            const int y = 78 + b * 20;
            const float dev = model.bandDeviationDb[b];

            g.setFont (Fonts::bodyLabel().withHeight (12.0f));
            g.setColour (tokens::bodyLabel);
            g.drawText (names[b], x, y, 66, 14, juce::Justification::centredLeft);

            g.setFont (Fonts::bodyValue().withHeight (12.0f));
            g.setColour (tokens::white);
            g.drawText (juce::String (model.bandLevelDb[b], 1),
                        x + 70, y, colW, 14, juce::Justification::centredRight);
            g.setColour (tokens::muted);
            g.drawText (juce::String (AnalysisEngine::targetBandDb (type, b), 1),
                        x + 70 + colW, y, colW, 14, juce::Justification::centredRight);
            g.setColour (std::abs (dev) <= 2.0f ? tokens::green
                          : std::abs (dev) <= 5.0f ? tokens::gold : tokens::red);
            g.drawText ((dev > 0 ? "+" : "") + juce::String (dev, 1),
                        x + 70 + colW * 2, y, colW, 14, juce::Justification::centredRight);
        }

        g.setFont (Fonts::make (9.5f));
        g.setColour (tokens::muted);
        g.drawText ("Targets follow the selected source type.",
                    x, 186, 260, 12, juce::Justification::centredLeft);
    }

    void drawScoreBreakdown (juce::Graphics& g, const AnalysisModel& model)
    {
        const int x = 556;

        g.setFont (Fonts::make (10.0f, false, true).withExtraKerningFactor (0.05f));
        g.setColour (tokens::muted);
        g.drawText ("SCORE BREAKDOWN", x, 36, 180, 12, juce::Justification::centredLeft);

        struct Part { const char* name; int value; const char* weight; };
        const Part parts[] = {
            { "Tone",  model.tone,  "24%" },  { "Punch", model.punch, "20%" },
            { "Level", model.level, "20%" },  { "Phase", model.phase, "16%" },
            { "Fit",   model.fit,   "20%" },
        };

        int y = 56;
        for (const auto& part : parts)
        {
            g.setFont (Fonts::bodyLabel().withHeight (12.0f));
            g.setColour (tokens::bodyLabel);
            g.drawText (part.name, x, y, 56, 14, juce::Justification::centredLeft);
            g.setFont (Fonts::make (9.5f));
            g.setColour (tokens::muted);
            g.drawText (part.weight, x + 52, y + 1, 34, 12, juce::Justification::centredLeft);
            g.setFont (Fonts::bodyValue().withHeight (12.0f));
            g.setColour (ScoreStatus::colour (part.value));
            g.drawText (juce::String (part.value), x + 90, y, 40, 14,
                        juce::Justification::centredRight);
            y += 19;
        }

        g.setColour (tokens::strokeSoft);
        g.fillRect (x, y + 2, 130, 1);
        y += 10;

        g.setFont (Fonts::bodyValue().withHeight (13.0f));
        g.setColour (tokens::white);
        g.drawText ("Source Score", x, y, 90, 16, juce::Justification::centredLeft);
        g.setFont (Fonts::make (16.0f, false, true));
        g.setColour (ScoreStatus::colour (model.score));
        g.drawText (juce::String (model.score), x + 90, y - 2, 40, 20,
                    juce::Justification::centredRight);

        g.setFont (Fonts::make (11.0f, false, true).withExtraKerningFactor (0.06f));
        g.drawText (ScoreStatus::phrase (model.score), x, y + 24, 130, 14,
                    juce::Justification::centredLeft);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override  { repaint(); }

    SourceGloProcessor& processor;
};

} // namespace sourceglo
