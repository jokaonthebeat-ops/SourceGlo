/*
    RescueTabView.h - the RESCUE tab: the full library browser. A dozen
    ranked matches on the left, the selected sample's detail on the right
    with preview and drag-out. Content region 742 x 263.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class RescueTabView : public juce::Component,
                      private juce::ChangeListener,
                      private juce::ListBoxModel,
                      private juce::Timer
{
public:
    explicit RescueTabView (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Rescue browser");

        matchList.setModel (this);
        matchList.setRowHeight (34);
        matchList.setColour (juce::ListBox::backgroundColourId, tokens::bg1);
        matchList.setColour (juce::ListBox::outlineColourId, tokens::strokeSoft);
        matchList.setOutlineThickness (1);
        addAndMakeVisible (matchList);

        previewButton.setTooltip ("Audition the selected sample");
        addAndMakeVisible (previewButton);
        previewButton.onClick = [this]
        {
            if (const auto* s = selected())
                processor.togglePreview (s->path);
        };

        favButton.setTooltip ("Favourite");
        favButton.setClickingTogglesState (true);
        addAndMakeVisible (favButton);
        favButton.onClick = [this]
        {
            if (const auto* s = selected())
                processor.getLibrary().setFavourite (s->path, favButton.getToggleState());
        };

        processor.analysisChanged.addChangeListener (this);
        processor.getLibrary().changed.addChangeListener (this);
        refresh();
        startTimerHz (5);
    }

    ~RescueTabView() override
    {
        processor.analysisChanged.removeChangeListener (this);
        processor.getLibrary().changed.removeChangeListener (this);
    }

    void resized() override
    {
        matchList.setBounds (24, 48, 380, 196);
        previewButton.setBounds (440, 196, 130, 34);
        favButton.setBounds (582, 200, 26, 26);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (Fonts::fieldLabel().withHeight (13.0f));
        g.setColour (tokens::white);
        g.drawText ("RESCUE BROWSER", 24, 20, 300, 16, juce::Justification::centredLeft);

        g.setFont (Fonts::make (10.0f));
        g.setColour (tokens::muted);
        g.drawText (juce::String ((int) matches.size()) + " ranked matches for the "
                      + currentTypeName() + " profile",
                    200, 22, 400, 14, juce::Justification::centredLeft);

        if (matches.empty())
        {
            g.setFont (Fonts::bodyLabel());
            g.setColour (tokens::muted);
            g.drawText (processor.getLibrary().getFolders().isEmpty()
                          ? "Add sample folders in the Library tab to browse matches."
                          : "No matches - rescan the library or add more folders.",
                        0, 126, 742, 16, juce::Justification::centred);
            return;
        }

        drawDetailPane (g);
    }

    // --- ListBoxModel -----------------------------------------------------
    int getNumRows() override                   { return (int) matches.size(); }

    void paintListBoxItem (int rowIndex, juce::Graphics& g, int width, int height,
                           bool selectedRow) override
    {
        if (rowIndex >= (int) matches.size())
            return;
        const auto& m = matches[(size_t) rowIndex];

        if (selectedRow)
        {
            g.setColour (tokens::panelHigh);
            g.fillRect (0, 0, width, height);
            g.setColour (tokens::cyan.withAlpha (0.6f));
            g.fillRect (0, 0, 2, height);
        }

        const bool playing = processor.getPreviewPath() == m.path;
        g.setColour (playing ? tokens::cyan : tokens::muted);
        g.setFont (Fonts::make (10.0f));
        g.drawText (juce::String (rowIndex + 1), 6, 0, 18, height, juce::Justification::centred);

        g.setFont (Fonts::rescueTitle().withHeight (13.0f));
        g.setColour (tokens::white);
        g.drawText (m.fileName, 30, 3, 200, 15, juce::Justification::centredLeft);
        g.setFont (Fonts::rescueTag());
        g.setColour (tokens::muted);
        g.drawText (m.tagA + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + m.tagB,
                    30, 18, 200, 12, juce::Justification::centredLeft);

        if (m.favourite)
            if (auto* star = Assets::icon ("star", tokens::gold))
                star->drawWithin (g, juce::Rectangle<float> ((float) width - 92.0f, 10.0f, 14.0f, 14.0f),
                                  juce::RectanglePlacement::centred, 1.0f);

        g.setFont (Fonts::make (14.0f, false, true));
        g.setColour (tokens::white);
        g.drawText (juce::String (m.fitPercent) + "%", width - 70, 0, 58, height,
                    juce::Justification::centredRight);
    }

    void selectedRowsChanged (int) override     { syncSelection(); repaint(); }

    void listBoxItemClicked (int rowIndex, const juce::MouseEvent& e) override
    {
        juce::ignoreUnused (rowIndex);
        dragArmed = e.mods.isLeftButtonDown();
    }

    void listBoxItemDoubleClicked (int rowIndex, const juce::MouseEvent&) override
    {
        if (rowIndex >= 0 && rowIndex < (int) matches.size())
            processor.togglePreview (matches[(size_t) rowIndex].path);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragArmed || e.getDistanceFromDragStart() < 10)
            return;
        if (const auto* s = selected(); s != nullptr && juce::File (s->path).existsAsFile())
        {
            dragArmed = false;
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ s->path }, false);
        }
    }

private:
    const RescueSuggestion* selected() const
    {
        const int row = matchList.getSelectedRow();
        return row >= 0 && row < (int) matches.size() ? &matches[(size_t) row] : nullptr;
    }

    juce::String currentTypeName() const
    {
        static const char* names[12] = { "Auto", "Kick", "Snare", "Clap", "808", "Bass",
                                         "Hat", "Percussion", "Loop", "Melody", "Vocal", "Other" };
        const int type = (int) processor.getAPVTS().getRawParameterValue (pid::sourceType)->load();
        return names[juce::jlimit (0, 11, type)];
    }

    void refresh()
    {
        const int type = (int) processor.getAPVTS().getRawParameterValue (pid::sourceType)->load();
        matches = processor.getLibrary().match (type, 12);
        matchList.updateContent();
        if (matchList.getSelectedRow() < 0 && ! matches.empty())
            matchList.selectRow (0);
        syncSelection();
    }

    void syncSelection()
    {
        if (const auto* s = selected())
        {
            favButton.setToggleState (s->favourite, juce::dontSendNotification);
            previewButton.setLabel (processor.getPreviewPath() == s->path ? "STOP" : "PREVIEW");
        }
    }

    void drawDetailPane (juce::Graphics& g)
    {
        const auto* s = selected();
        if (s == nullptr)
            return;

        const int x = 440;

        g.setFont (Fonts::rescueTitle().withHeight (15.0f));
        g.setColour (tokens::white);
        g.drawText (s->fileName, x, 48, 280, 18, juce::Justification::centredLeft);

        g.setFont (Fonts::rescueTag().withHeight (12.0f));
        g.setColour (tokens::muted);
        g.drawText (s->tagA + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + s->tagB
                      + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))
                      + juce::String (s->fitPercent) + "% fit",
                    x, 68, 280, 14, juce::Justification::centredLeft);

        // Big waveform overview.
        const juce::Rectangle<int> wave (x, 92, 280, 84);
        g.setColour (tokens::bg1);
        g.fillRoundedRectangle (wave.toFloat(), 4.0f);
        const bool playing = processor.getPreviewPath() == s->path;
        g.setColour (playing ? tokens::cyan : tokens::cyanMid.withAlpha (0.8f));
        const float barW = (float) wave.getWidth() / (float) RescueSuggestion::waveformBars;
        for (int b = 0; b < RescueSuggestion::waveformBars; ++b)
        {
            const float amp = juce::jlimit (0.04f, 1.0f, s->waveform[(size_t) b]);
            const float bh = amp * (float) (wave.getHeight() - 10);
            g.fillRect ((float) wave.getX() + 4.0f + (float) b * barW * 0.985f,
                        (float) wave.getCentreY() - bh * 0.5f,
                        juce::jmax (1.0f, barW - 2.5f), bh);
        }

        g.setFont (Fonts::make (9.5f));
        g.setColour (tokens::muted);
        g.drawText ("Drag a row into your DAW to use it.",
                    x, 238, 280, 12, juce::Justification::centredLeft);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        refresh();
        repaint();
    }

    void timerCallback() override
    {
        if (! isShowing() && ! headlessRefreshMode())
            return;
        const auto now = processor.getPreviewPath();
        if (now != lastPreview)
        {
            lastPreview = now;
            syncSelection();
            repaint();
            matchList.repaint();
        }
    }

    SourceGloProcessor& processor;
    std::vector<RescueSuggestion> matches;
    juce::ListBox matchList { "Ranked matches" };
    AssetButton previewButton { "Preview", ButtonKind::smallCyan, "PREVIEW", "play" };
    IconButton favButton { "Favourite", "star", tokens::muted, tokens::gold };
    bool dragArmed = false;
    juce::String lastPreview;
};

} // namespace sourceglo
