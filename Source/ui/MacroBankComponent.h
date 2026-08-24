/*
    MacroBankComponent - the eight macro filmstrip knobs.
    Bounds: layout::macroStrip {20,848,993,121} - the mockup's strip visually
    spans beneath both the source panel and the lower main panel, wider than
    macros_panel in the layout JSON; the shell wells arbitrate (see Theme.h).
    Coordinates are strip-local.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class MacroBankComponent : public juce::Component
{
public:
    explicit MacroBankComponent (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Macro bank");

        for (int i = 0; i < 8; ++i)
        {
            auto& knob = knobs[(size_t) i];
            knob = std::make_unique<FilmstripKnob> (false, juce::String (macroNames[i]));
            knob->setTooltip (juce::String (macroNames[i]) + " macro");
            addAndMakeVisible (*knob);

            attachments[(size_t) i] =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    processor.getAPVTS(), macroParams[i], *knob);

            knob->onValueChange = [this, i] { repaint (valueArea (i)); };
        }
    }

    void resized() override
    {
        // Mockup: labels ~y 883, 70 px knobs at ~900..970, values ~976..992
        // (strip-local: component sits at y 848).
        for (int i = 0; i < 8; ++i)
            knobs[(size_t) i]->setBounds (centreX (i) - 35, 52, 70, 70);
    }

    void paint (juce::Graphics& g) override
    {
        // Vertical MACROS label at the far left.
        {
            juce::Graphics::ScopedSaveState save (g);
            g.addTransform (juce::AffineTransform::rotation (
                -juce::MathConstants<float>::halfPi, 13.0f, 84.0f));
            g.setFont (Fonts::fieldLabel().withHeight (11.0f));
            g.setColour (tokens::muted);
            g.drawText ("MACROS", -32, 77, 90, 14, juce::Justification::centred);
        }

        for (int i = 0; i < 8; ++i)
        {
            g.setFont (Fonts::fieldLabel());
            g.setColour (tokens::text);
            g.drawText (juce::String (macroNames[i]).toUpperCase(),
                        centreX (i) - 55, 32, 110, 14, juce::Justification::centred);

            g.setFont (Fonts::bodyValue().withHeight (13.0f));
            g.setColour (tokens::white);
            g.drawText (valueText (i), valueArea (i), juce::Justification::centred);
        }
    }

private:
    // Mockup-measured centres (design-global 110..954, strip-local -20).
    // NOTE: the approved mockup's spacing is NOT mathematically even (steps
    // 121,112,110,115,134,128,124) - the reference is the visual authority,
    // so these match it; the QA sheet's "evenly spaced" line is answered in
    // the documented-differences list.
    static int centreX (int i)
    {
        static constexpr int centres[8] = { 90, 211, 323, 433, 548, 682, 810, 934 };
        return centres[juce::jlimit (0, 7, i)];
    }

    juce::Rectangle<int> valueArea (int i) const
    {
        return { centreX (i) - 45, 128, 90, 14 };
    }

    juce::String valueText (int i) const
    {
        const double v = knobs[(size_t) i]->getValue();
        return i == 7 ? juce::String (v, 1) + " dB"    // Output macro is in dB
                      : juce::String (juce::roundToInt (v));
    }

    SourceGloProcessor& processor;

    static constexpr const char* macroNames[8] =
        { "Punch", "Body", "Tone", "Air", "Stereo", "Transients", "Saturate", "Output" };
    const char* macroParams[8] =
        { pid::punch, pid::body, pid::tone, pid::air,
          pid::stereo, pid::transients, pid::saturate, pid::outputGain };

    std::array<std::unique_ptr<FilmstripKnob>, 8> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> attachments;
};

} // namespace sourceglo
