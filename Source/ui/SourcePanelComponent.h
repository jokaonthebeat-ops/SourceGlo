/*
    SourcePanelComponent - source type, input/output routing, trims, meters and
    the live source stats. Bounds: layout::sourcePanel {10,75,243,894};
    child coordinates are panel-local.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class SourcePanelComponent : public juce::Component, private juce::Timer,
                             private juce::ChangeListener
{
public:
    explicit SourcePanelComponent (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Source panel");

        addAndMakeVisible (typeDropdown);
        typeDropdown.onChange = [this] (int i)
        {
            if (auto* param = processor.getAPVTS().getParameter (pid::sourceType))
                param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
        };
        typeDropdown.setSelectedIndex (
            (int) processor.getAPVTS().getRawParameterValue (pid::sourceType)->load(),
            juce::dontSendNotification);


        addAndMakeVisible (inMeterL);
        addAndMakeVisible (inMeterR);
        addAndMakeVisible (outMeterL);
        addAndMakeVisible (outMeterR);

        inputTrim.setTooltip ("Input gain trim");
        outputTrim.setTooltip ("Output gain trim");
        addAndMakeVisible (inputTrim);
        addAndMakeVisible (outputTrim);

        inputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getAPVTS(), pid::inputGain, inputTrim);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getAPVTS(), pid::outputGain, outputTrim);

        invButton.setTooltip ("Invert input phase");
        monoButton.setTooltip ("Sum output to mono");
        addAndMakeVisible (invButton);
        addAndMakeVisible (monoButton);
        invAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.getAPVTS(), pid::phaseInvert, invButton);
        monoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.getAPVTS(), pid::mono, monoButton);

        inputTrim.onValueChange  = [this] { repaint (trimValueArea (true)); };
        outputTrim.onValueChange = [this] { repaint (trimValueArea (false)); };

        processor.analysisChanged.addChangeListener (this);
        startTimerHz (30);   // meters; stats repaint every 3rd tick (10 Hz)
    }

    ~SourcePanelComponent() override
    {
        processor.analysisChanged.removeChangeListener (this);
    }

    // Headless tools: open the real source-type menu for the film.
    void openTypeMenu()   { typeDropdown.openMenu(); }

    void resized() override
    {
        typeDropdown.setBounds   (14,  91, 205, 32);

        inMeterL.setBounds  (47, 203, 22, 78);
        inMeterR.setBounds  (71, 203, 22, 78);
        inputTrim.setBounds (146, 203, 44, 44);
        invButton.setBounds (148, 252, 46, 18);

        outMeterL.setBounds  (47, 353, 22, 78);
        outMeterR.setBounds  (71, 353, 22, 78);
        outputTrim.setBounds (146, 353, 44, 44);
        monoButton.setBounds (148, 402, 46, 18);
    }

    void paint (juce::Graphics& g) override
    {
        auto title = [&g] (const juce::String& text, int x, int y, juce::Colour c)
        {
            g.setColour (c);
            g.drawText (text, x, y, 210, 18, juce::Justification::centredLeft);
        };

        g.setFont (Fonts::panelTitle());
        title ("SOURCE", 28, 24, tokens::white);
        g.setColour (tokens::strokeSoft);
        g.fillRect (14, 52, 215, 1);

        g.setFont (Fonts::fieldLabel());
        title ("SOURCE TYPE", 28, 70, tokens::text);
        title ("INPUT",       28, 148, tokens::text);
        title ("OUTPUT",      28, 301, tokens::text);
        title ("SOURCE STATS",28, 455, tokens::text);

        // Input / output readouts: what the plugin actually sits on. The
        // input shows the host's track name where the host provides one -
        // the honest version of the mockup's "Track 07 - Kick" sample text.
        drawReadout (g, { 14, 167, 205, 28 }, processor.getInputDisplayName());
        drawReadout (g, { 14, 317, 205, 28 }, processor.getOutputDisplayName());

        drawMeterScale (g, 44, 203, 78);
        drawMeterScale (g, 44, 353, 78);

        // Trim readouts
        g.setFont (Fonts::bodyValue());
        g.setColour (tokens::white);
        g.drawText (formatDb ((float) inputTrim.getValue()), trimValueArea (true),
                    juce::Justification::centredLeft);
        g.drawText (formatDb ((float) outputTrim.getValue()), trimValueArea (false),
                    juce::Justification::centredLeft);

        // Stats: peak/RMS/crest/true-peak run live from the processor's
        // meters (with hold); duration/tempo/key come from the last analysis.
        const auto& model = processor.getAnalysis();
        auto dbText = [] (float db, const char* unit) -> juce::String
        {
            if (db <= -90.0f) return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
            return juce::String (db, 1) + " " + unit;
        };
        const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x93"));

        const std::pair<const char*, juce::String> rows[] = {
            { "Peak",         dbText (holdPeakDb, "dBFS") },
            { "RMS",          dbText (holdRmsDb, "dBFS") },
            { "Crest Factor", holdPeakDb > -90.0f ? juce::String (holdPeakDb - holdRmsDb, 1) + " dB" : dash },
            { "True Peak",    dbText (holdTruePeakDb, "dBTP") },
            { "Duration",     model.analyzed ? juce::String (model.stats.durationSec, 2) + " s" : dash },
            { "Tempo",        model.analyzed && model.stats.tempoBpm > 0.0f
                                ? juce::String (model.stats.tempoBpm, 1) + " BPM" : dash },
            { "Key",          model.analyzed && model.stats.key.isNotEmpty() ? model.stats.key : dash },
        };

        int y = 481;
        for (const auto& row : rows)
        {
            g.setFont (Fonts::bodyLabel());
            g.setColour (tokens::bodyLabel);
            g.drawText (row.first, 28, y, 110, 16, juce::Justification::centredLeft);
            g.setFont (Fonts::bodyValue());
            g.setColour (tokens::white);
            g.drawText (row.second, 110, y, 105, 16, juce::Justification::centredRight);
            y += 25;
        }
    }

private:
    void drawReadout (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text)
    {
        auto art = Assets::dropdown();
        if (art.isValid())
            g.drawImage (art, r.toFloat(), juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (tokens::panelHigh);
            g.fillRoundedRectangle (r.toFloat(), 5.0f);
            g.setColour (tokens::stroke);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, 1.0f);
        }
        g.setColour (tokens::white);
        g.setFont (Fonts::bodyValue());
        g.drawText (text, r.reduced (12, 0), juce::Justification::centredLeft, true);
    }

    juce::Rectangle<int> trimValueArea (bool input) const
    {
        return { 196, input ? 217 : 367, 46, 16 };
    }

    static juce::String formatDb (float v)
    {
        return juce::String (v, 1) + " dB";
    }

    void drawMeterScale (juce::Graphics& g, int rightX, int topY, int height)
    {
        g.setFont (Fonts::make (8.0f));
        g.setColour (tokens::muted.withAlpha (0.8f));
        const int marks[] = { -6, -12, -18, -24, -30, -48 };
        int i = 0;
        for (int m : marks)
        {
            const int y = topY + (int) ((float) i / 5.0f * (float) (height - 8));
            g.drawText (juce::String (m), rightX - 26, y, 24, 8, juce::Justification::centredRight);
            ++i;
        }
    }

    void timerCallback() override
    {
        if (! isShowing() && ! headlessRefreshMode())
            return;

        auto toDb = [] (float v) { return juce::Decibels::gainToDecibels (v, -60.0f); };
        inMeterL.setLevel (toDb (processor.inPeak[0].load()));
        inMeterR.setLevel (toDb (processor.inPeak[1].load()));
        outMeterL.setLevel (toDb (processor.outPeak[0].load()));
        outMeterR.setLevel (toDb (processor.outPeak[1].load()));

        if (++statsTick >= 3)
        {
            statsTick = 0;

            // The dropdown pushes changes to the parameter but must also
            // follow it: presets, host automation and undo all move it, and
            // reading it once at construction left the control showing a
            // source type the plugin was not using.
            const int typeNow = (int) processor.getAPVTS()
                                    .getRawParameterValue (pid::sourceType)->load();
            if (typeNow != typeDropdown.getSelectedIndex())
                typeDropdown.setSelectedIndex (typeNow, juce::dontSendNotification);

            // Live stat hold: capture the loudest recent values, decay slowly
            // so the readout is legible rather than flickering per block.
            const float peakNow = juce::Decibels::gainToDecibels (
                juce::jmax (processor.inPeak[0].load(), processor.inPeak[1].load()), -120.0f);
            const float rmsNow = juce::Decibels::gainToDecibels (
                0.5f * (processor.inRms[0].load() + processor.inRms[1].load()), -120.0f);
            const float tpNow = processor.truePeakSinceDb();

            holdPeakDb     = juce::jmax (holdPeakDb - 0.8f, peakNow);
            holdRmsDb      = juce::jmax (holdRmsDb - 0.8f, rmsNow);
            holdTruePeakDb = juce::jmax (holdTruePeakDb - 0.8f, tpNow);

            repaint (14, 455, 215, 210);   // stats block only
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        repaint (14, 167, 205, 28);    // input readout (host track name)
        repaint (14, 455, 215, 210);   // stats block
    }

    SourceGloProcessor& processor;

    AssetDropdown typeDropdown   { "Source type",
        { "Auto", "Kick", "Snare", "Clap", "808", "Bass", "Hat",
          "Percussion", "Loop", "Melody", "Vocal", "Other" }, 1 };

    VerticalMeter inMeterL, inMeterR, outMeterL, outMeterR;
    FilmstripKnob inputTrim  { true, "Input trim" };
    FilmstripKnob outputTrim { true, "Output trim" };
    SmallPill invButton  { "Phase invert", juce::String (juce::CharPointer_UTF8 ("\xc3\x98 INV")) };
    SmallPill monoButton { "Mono", "MONO" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment, outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> invAttachment, monoAttachment;

    int statsTick = 0;
    float holdPeakDb = -120.0f, holdRmsDb = -120.0f, holdTruePeakDb = -120.0f;
};

} // namespace sourceglo
