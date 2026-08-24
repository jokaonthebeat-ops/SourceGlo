/*
    FooterStatusComponent - HQ / oversampling readouts, the tagline, sidechain
    badge and the UI-scale control. Bounds: layout::footer {6,976,1479,72};
    coordinates are local.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class FooterStatusComponent : public juce::Component
{
public:
    explicit FooterStatusComponent (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Status footer");

        hqButton.setTooltip ("High quality processing");
        addAndMakeVisible (hqButton);
        hqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.getAPVTS(), pid::hq, hqButton);

        scaleSlider.setRange (0.7, 1.5, 0.0);
        scaleSlider.setValue (1.0, juce::dontSendNotification);
        scaleSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        scaleSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        scaleSlider.setTitle ("UI scale");
        scaleSlider.setTooltip ("Resize the interface");
        addAndMakeVisible (scaleSlider);
        scaleSlider.onValueChange = [this]
        {
            if (! updatingFromEditor && onScaleChanged)
                onScaleChanged ((float) scaleSlider.getValue());
        };

        expandButton.setTooltip ("Reset to 100 %");
        expandButton.setIconPadding (4.0f);
        addAndMakeVisible (expandButton);
        expandButton.onClick = [this] { if (onScaleChanged) onScaleChanged (1.0f); };
    }

    std::function<void (float)> onScaleChanged;

    void setDisplayedScale (float s)
    {
        updatingFromEditor = true;
        scaleSlider.setValue (s, juce::dontSendNotification);
        updatingFromEditor = false;
        repaint (1200, 34, 70, 26);
    }

    void resized() override
    {
        hqButton.setBounds (22, 38, 40, 22);
        scaleSlider.setBounds (1273, 38, 146, 22);
        expandButton.setBounds (1431, 38, 22, 22);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (Fonts::footer());
        g.setColour (tokens::bodyLabel);
        g.drawText ("OVSPL:", 75, 40, 46, 18, juce::Justification::centredLeft);
        g.setColour (tokens::white);
        const int ovsIndex = (int) processor.getAPVTS().getRawParameterValue (pid::oversampling)->load();
        const char* ovsNames[] = { "Off", "2x", "4x", "8x" };
        g.drawText (ovsNames[juce::jlimit (0, 3, ovsIndex)], 122, 40, 30, 18,
                    juce::Justification::centredLeft);

        // Tagline, centred on the canvas.
        g.setFont (Fonts::footer().withHeight (14.0f));
        g.setColour (tokens::cyan);
        g.drawText ("Production Intelligence for Better Mixes",
                    0, 40, 1479, 18, juce::Justification::centred);

        // Sidechain badge (inactive until the engine milestone).
        const juce::Rectangle<float> sc (1175.0f, 38.0f, 34.0f, 22.0f);
        g.setColour (tokens::stroke);
        g.drawRoundedRectangle (sc, 4.0f, 1.0f);
        g.setColour (tokens::muted);
        g.setFont (Fonts::make (10.0f, false, true));
        g.drawText ("SC", sc.toNearestInt(), juce::Justification::centred);

        // Scale percentage readout.
        g.setColour (tokens::text);
        g.setFont (Fonts::footer());
        g.drawText (juce::String (juce::roundToInt (scaleSlider.getValue() * 100.0)) + "%",
                    1222, 40, 48, 18, juce::Justification::centredRight);
    }

private:
    SourceGloProcessor& processor;

    SmallPill hqButton { "HQ", "HQ" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hqAttachment;

    juce::Slider scaleSlider;
    IconButton expandButton { "Reset scale", "expand" };
    bool updatingFromEditor = false;
};

} // namespace sourceglo
