/*
    RescueSuggestionsComponent - five recommendation rows on the supplied row
    art, Auto Match toggle and the Browse Library action.
    Bounds: layout::rescuePanel {1017,528,464,441}; coordinates are local.
    List rows: layout::rescueList {1034,580,388,274} -> local x 17, y 52,
    row height 48, pitch 56.5.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class RescueSuggestionsComponent : public juce::Component
{
public:
    explicit RescueSuggestionsComponent (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Rescue suggestions");

        helpButton.setTooltip ("Suggestions ranked by fit against the current source");
        helpButton.setIconPadding (2.0f);
        addAndMakeVisible (helpButton);

        autoMatchToggle.setTooltip ("Automatically refresh suggestions after analysis");
        addAndMakeVisible (autoMatchToggle);
        autoMatchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.getAPVTS(), pid::autoMatch, autoMatchToggle);

        for (int i = 0; i < 5; ++i)
        {
            auto& play = playButtons[(size_t) i];
            play = std::make_unique<IconButton> ("Preview " + rowName (i), "play",
                                                 tokens::text, tokens::cyan);
            play->setTooltip ("Preview " + rowName (i));
            play->setCircled (true);
            play->setIconPadding (8.0f);
            addAndMakeVisible (*play);
            play->onClick = [this, i] { playingIndex = playingIndex == i ? -1 : i; repaint(); };

            auto& star = starButtons[(size_t) i];
            star = std::make_unique<IconButton> ("Favourite " + rowName (i), "star",
                                                 tokens::muted, tokens::gold);
            star->setTooltip ("Favourite");
            star->setClickingTogglesState (true);
            addAndMakeVisible (*star);
        }

        browseButton.setTooltip ("Open the sample library browser");
        browseButton.setIconTint (tokens::text);
        addAndMakeVisible (browseButton);

        moreButton.setTooltip ("Library options");
        addAndMakeVisible (moreButton);
    }

    void resized() override
    {
        helpButton.setBounds (296, 28, 18, 18);
        autoMatchToggle.setBounds (409, 27, 36, 20);

        for (int i = 0; i < 5; ++i)
        {
            const auto row = rowBounds (i);
            playButtons[(size_t) i]->setBounds (row.getX() + 9,  row.getY() + 10, 28, 28);
            starButtons[(size_t) i]->setBounds (row.getX() + 356, row.getY() + 12, 24, 24);
        }

        browseButton.setBounds (49, 377, 360, 45);
        moreButton.setBounds (424, 387, 26, 26);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& model = processor.getAnalysis();

        g.setFont (Fonts::panelTitle().withHeight (15.0f));
        g.setColour (tokens::white);
        g.drawText ("RESCUE SUGGESTIONS", 30, 28, 240, 18, juce::Justification::centredLeft);

        g.setFont (Fonts::make (10.0f, false, true).withExtraKerningFactor (0.05f));
        g.setColour (tokens::muted);
        g.drawText ("AUTO MATCH", 330, 30, 76, 14, juce::Justification::centredRight);

        for (int i = 0; i < (int) model.rescues.size() && i < 5; ++i)
            drawRow (g, i, model.rescues[(size_t) i]);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int idx = rowIndexAt (e.getPosition());
        if (idx != hoverIndex) { hoverIndex = idx; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverIndex != -1) { hoverIndex = -1; repaint(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int idx = rowIndexAt (e.getPosition());
        if (idx >= 0 && idx != selectedIndex)
        {
            selectedIndex = idx;
            repaint();
        }
    }

private:
    juce::Rectangle<int> rowBounds (int i) const
    {
        // Mockup-measured: rows at x 1070, tops 600 + 57.5 i (panel-local
        // x 53, y 72) - the layout JSON's rescue_list sits ~35 px left and
        // ~20 px high of where the reference actually draws the rows.
        return { 53, 72 + juce::roundToInt (57.5f * (float) i), 388, 48 };
    }

    int rowIndexAt (juce::Point<int> pos) const
    {
        for (int i = 0; i < 5; ++i)
            if (rowBounds (i).contains (pos))
                return i;
        return -1;
    }

    juce::String rowName (int i) const
    {
        const auto& r = processor.getAnalysis().rescues;
        return i < (int) r.size() ? r[(size_t) i].fileName : juce::String();
    }

    void drawRow (juce::Graphics& g, int i, const RescueSuggestion& item)
    {
        const auto row = rowBounds (i);
        const int state = i == selectedIndex ? 2 : (i == hoverIndex ? 1 : 0);

        auto art = Assets::rescueRow (state);
        if (art.isValid())
            g.drawImage (art, row.toFloat(), juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (state == 2 ? tokens::panelHigh : tokens::panelAlt);
            g.fillRoundedRectangle (row.toFloat(), 6.0f);
            if (state == 2)
            {
                g.setColour (tokens::cyan);
                g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 6.0f, 1.2f);
            }
        }

        drawWaveform (g, { row.getX() + 58, row.getY() + 10, 50, 28 }, item.waveformSeed,
                      i == playingIndex);

        g.setFont (Fonts::rescueTitle());
        g.setColour (tokens::white);
        g.drawText (item.fileName, row.getX() + 125, row.getY() + 7, 145, 16,
                    juce::Justification::centredLeft);

        g.setFont (Fonts::rescueTag());
        g.setColour (tokens::muted);
        g.drawText (item.tagA + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + item.tagB,
                    row.getX() + 125, row.getY() + 26, 145, 13,
                    juce::Justification::centredLeft);

        g.setFont (Fonts::make (15.0f, false, true));
        g.setColour (tokens::white);
        g.drawText (juce::String (item.fitPercent) + "%",
                    row.getX() + 274, row.getY() + 8, 52, 16, juce::Justification::centredRight);

        g.setFont (Fonts::make (9.5f).withExtraKerningFactor (0.06f));
        g.setColour (tokens::muted);
        g.drawText ("FIT", row.getX() + 274, row.getY() + 26, 52, 11,
                    juce::Justification::centredRight);
    }

    void drawWaveform (juce::Graphics& g, juce::Rectangle<int> r,
                       juce::uint32 seed, bool playing)
    {
        juce::Random rng ((juce::int64) seed * 7919);
        const int bars = 24;
        const float barW = (float) r.getWidth() / (float) bars;

        g.setColour (playing ? tokens::cyan : tokens::cyanMid.withAlpha (0.75f));
        for (int b = 0; b < bars; ++b)
        {
            // Percussive envelope: hot attack, decaying tail.
            const float env = std::exp (-3.2f * (float) b / (float) bars);
            const float amp = juce::jlimit (0.06f, 1.0f, env * (0.55f + rng.nextFloat() * 0.6f));
            const float bh  = amp * (float) r.getHeight();
            g.fillRect ((float) r.getX() + (float) b * barW,
                        (float) r.getCentreY() - bh * 0.5f,
                        juce::jmax (1.0f, barW - 1.2f), bh);
        }
    }

    SourceGloProcessor& processor;

    IconButton helpButton { "Suggestions help", "help" };
    TogglePill autoMatchToggle { "Auto Match" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoMatchAttachment;

    std::array<std::unique_ptr<IconButton>, 5> playButtons, starButtons;

    AssetButton browseButton { "Browse Library", ButtonKind::browseLibrary,
                               "BROWSE LIBRARY", "folder" };
    IconButton moreButton { "Library options", "menu" };

    int selectedIndex = 0, hoverIndex = -1, playingIndex = -1;
};

} // namespace sourceglo
