#pragma once
#include <JuceHeader.h>

class SourceGloFilmstripLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SourceGloFilmstripLookAndFeel (juce::Image macroStripIn,
                                   juce::Image trimStripIn)
        : macroStrip (std::move (macroStripIn)),
          trimStrip  (std::move (trimStripIn)) {}

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float,
                           juce::Slider& slider) override
    {
        const bool isTrim = slider.getProperties().getWithDefault ("sourcegloTrim", false);
        const auto& strip = isTrim ? trimStrip : macroStrip;
        if (! strip.isValid())
            return;

        constexpr int frames = 128;
        const int frameHeight = strip.getHeight() / frames;
        const int frame = juce::jlimit (0, frames - 1,
                                        juce::roundToInt (sliderPos * (frames - 1)));
        const int drawSize = juce::jmin (width, height);
        const int dx = x + (width - drawSize) / 2;
        const int dy = y + (height - drawSize) / 2;

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (strip,
                     dx, dy, drawSize, drawSize,
                     0, frame * frameHeight, strip.getWidth(), frameHeight);
    }

private:
    juce::Image macroStrip, trimStrip;
};
