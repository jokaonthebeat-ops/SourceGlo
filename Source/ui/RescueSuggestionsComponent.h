/*
    RescueSuggestionsComponent - live suggestions from the user's own sample
    library, ranked by fit against the selected source type. Rows preview
    through the plugin output, drag out into the host as real files, and
    star into persistent favourites.
    Bounds: layout::rescuePanel {1017,528,464,441}; coordinates are local.
    Rows: mockup-measured x 53, y 72, pitch 57.5.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class RescueSuggestionsComponent : public juce::Component,
                                   private juce::ChangeListener,
                                   private juce::Timer
{
public:
    explicit RescueSuggestionsComponent (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Rescue suggestions");

        helpButton.setTooltip ("Suggestions from your library, ranked by fit for the source type");
        helpButton.setIconPadding (2.0f);
        addAndMakeVisible (helpButton);

        autoMatchToggle.setTooltip ("Refresh suggestions automatically after analysis and scans");
        addAndMakeVisible (autoMatchToggle);
        autoMatchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.getAPVTS(), pid::autoMatch, autoMatchToggle);

        for (int i = 0; i < 5; ++i)
        {
            auto& play = playButtons[(size_t) i];
            play = std::make_unique<IconButton> ("Preview suggestion", "play",
                                                 tokens::text, tokens::cyan);
            play->setCircled (true);
            play->setIconPadding (8.0f);
            addChildComponent (*play);
            play->onClick = [this, i]
            {
                const auto& rescues = processor.getAnalysis().rescues;
                if (i < (int) rescues.size())
                    processor.togglePreview (rescues[(size_t) i].path);
            };

            auto& star = starButtons[(size_t) i];
            star = std::make_unique<IconButton> ("Favourite", "star",
                                                 tokens::muted, tokens::gold);
            star->setClickingTogglesState (true);
            star->setTooltip ("Favourite");
            addChildComponent (*star);
            star->onClick = [this, i]
            {
                const auto& rescues = processor.getAnalysis().rescues;
                if (i < (int) rescues.size())
                    processor.getLibrary().setFavourite (rescues[(size_t) i].path,
                                                         starButtons[(size_t) i]->getToggleState());
            };
        }

        browseButton.setTooltip ("Manage the sample library folders");
        browseButton.setIconTint (tokens::text);
        addAndMakeVisible (browseButton);
        browseButton.onClick = [this] { if (onBrowseLibrary) onBrowseLibrary(); };

        moreButton.setTooltip ("Library options");
        addAndMakeVisible (moreButton);
        moreButton.onClick = browseButton.onClick;

        processor.analysisChanged.addChangeListener (this);
        processor.getLibrary().changed.addChangeListener (this);
        startTimerHz (5);          // preview auto-stop / play-state sync
        syncRows();
    }

    ~RescueSuggestionsComponent() override
    {
        processor.analysisChanged.removeChangeListener (this);
        processor.getLibrary().changed.removeChangeListener (this);
    }

    std::function<void()> onBrowseLibrary;    // wired by the editor (Library tab)

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
        const auto& rescues = processor.getAnalysis().rescues;

        g.setFont (Fonts::panelTitle().withHeight (15.0f));
        g.setColour (tokens::white);
        g.drawText ("RESCUE SUGGESTIONS", 30, 28, 240, 18, juce::Justification::centredLeft);

        g.setFont (Fonts::make (10.0f, false, true).withExtraKerningFactor (0.05f));
        g.setColour (tokens::muted);
        g.drawText ("AUTO MATCH", 330, 30, 76, 14, juce::Justification::centredRight);

        for (int i = 0; i < (int) rescues.size() && i < 5; ++i)
            drawRow (g, i, rescues[(size_t) i]);

        if (rescues.empty())
        {
            auto& lib = processor.getLibrary();
            g.setFont (Fonts::bodyLabel());
            g.setColour (tokens::muted);
            if (lib.getFolders().isEmpty())
            {
                g.drawText ("No sample folders yet.", 53, 200, 388, 16,
                            juce::Justification::centred);
                g.drawText ("Use Browse Library to add your packs.", 53, 220, 388, 16,
                            juce::Justification::centred);
            }
            else if (lib.isScanning())
                g.drawText ("Scanning your library" + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")),
                            53, 210, 388, 16, juce::Justification::centred);
            else
                g.drawText ("No matches yet - rescan or add more folders.",
                            53, 210, 388, 16, juce::Justification::centred);
        }
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
        dragStarted = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Drag a suggestion straight into the host as a file.
        if (dragStarted || e.getDistanceFromDragStart() < 10)
            return;
        const int idx = rowIndexAt (e.getMouseDownPosition());
        const auto& rescues = processor.getAnalysis().rescues;
        if (idx >= 0 && idx < (int) rescues.size()
             && juce::File (rescues[(size_t) idx].path).existsAsFile())
        {
            dragStarted = true;
            juce::DragAndDropContainer::performExternalDragDropOfFiles (
                { rescues[(size_t) idx].path }, false);
        }
    }

private:
    juce::Rectangle<int> rowBounds (int i) const
    {
        return { 53, 72 + juce::roundToInt (57.5f * (float) i), 388, 48 };
    }

    int rowIndexAt (juce::Point<int> pos) const
    {
        const int count = juce::jmin (5, (int) processor.getAnalysis().rescues.size());
        for (int i = 0; i < count; ++i)
            if (rowBounds (i).contains (pos))
                return i;
        return -1;
    }

    void syncRows()
    {
        const auto& rescues = processor.getAnalysis().rescues;
        const auto previewing = processor.getPreviewPath();

        for (int i = 0; i < 5; ++i)
        {
            const bool present = i < (int) rescues.size();
            playButtons[(size_t) i]->setVisible (present);
            starButtons[(size_t) i]->setVisible (present);
            if (present)
            {
                playButtons[(size_t) i]->setToggleState (
                    previewing == rescues[(size_t) i].path, juce::dontSendNotification);
                starButtons[(size_t) i]->setToggleState (
                    rescues[(size_t) i].favourite, juce::dontSendNotification);
            }
        }
        selectedIndex = juce::jmin (selectedIndex, (int) rescues.size() - 1);
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

        drawWaveform (g, { row.getX() + 58, row.getY() + 10, 50, 28 }, item,
                      processor.getPreviewPath() == item.path);

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
                       const RescueSuggestion& item, bool playing)
    {
        const int bars = RescueSuggestion::waveformBars;
        const float barW = (float) r.getWidth() / (float) bars;

        g.setColour (playing ? tokens::cyan : tokens::cyanMid.withAlpha (0.75f));
        for (int b = 0; b < bars; ++b)
        {
            const float amp = juce::jlimit (0.05f, 1.0f, item.waveform[(size_t) b]);
            const float bh = amp * (float) r.getHeight();
            g.fillRect ((float) r.getX() + (float) b * barW,
                        (float) r.getCentreY() - bh * 0.5f,
                        juce::jmax (1.0f, barW - 1.2f), bh);
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        syncRows();
        repaint();
    }

    void timerCallback() override
    {
        if (! isShowing() && ! headlessRefreshMode())
            return;
        // Preview can end on its own; keep the play buttons honest.
        const auto previewing = processor.getPreviewPath();
        if (previewing != lastPreview)
        {
            lastPreview = previewing;
            syncRows();
            repaint();
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

    int selectedIndex = 0, hoverIndex = -1;
    bool dragStarted = false;
    juce::String lastPreview;
};

} // namespace sourceglo
