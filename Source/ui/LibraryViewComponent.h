/*
    LibraryViewComponent - the LIBRARY tab: manage the sample folders the
    rescue engine draws from, watch scan progress, see what is indexed.
    Sits in the analysis tabs' content region (742 x 263 local).
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class LibraryViewComponent : public juce::Component,
                             private juce::ChangeListener,
                             private juce::ListBoxModel,
                             private juce::Timer
{
public:
    explicit LibraryViewComponent (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Sample library");

        folderList.setModel (this);
        folderList.setRowHeight (28);
        folderList.setColour (juce::ListBox::backgroundColourId, tokens::bg1);
        folderList.setColour (juce::ListBox::outlineColourId, tokens::strokeSoft);
        folderList.setOutlineThickness (1);
        addAndMakeVisible (folderList);

        addButton.setTooltip ("Add a folder of samples to the library");
        addButton.setIconTint (tokens::cyan);
        addAndMakeVisible (addButton);
        addButton.onClick = [this] { chooseFolder(); };

        removeButton.setTooltip ("Remove the selected folder and its samples");
        addAndMakeVisible (removeButton);
        removeButton.onClick = [this]
        {
            const int row = folderList.getSelectedRow();
            const auto folders = processor.getLibrary().getFolders();
            if (row >= 0 && row < folders.size())
                processor.getLibrary().removeFolder (juce::File (folders[row]));
        };

        rescanButton.setTooltip ("Rescan every folder for new or changed samples");
        addAndMakeVisible (rescanButton);
        rescanButton.onClick = [this] { processor.getLibrary().rescan(); };

        processor.getLibrary().changed.addChangeListener (this);
        startTimerHz (4);      // scan progress readout
    }

    ~LibraryViewComponent() override
    {
        processor.getLibrary().changed.removeChangeListener (this);
    }

    void resized() override
    {
        folderList.setBounds (24, 56, 420, 160);
        addButton.setBounds    (468, 56, 120, 34);
        removeButton.setBounds (468, 98, 120, 34);
        rescanButton.setBounds (468, 140, 120, 34);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (Fonts::fieldLabel().withHeight (13.0f));
        g.setColour (tokens::white);
        g.drawText ("SAMPLE LIBRARY", 24, 30, 250, 16, juce::Justification::centredLeft);

        auto& lib = processor.getLibrary();
        g.setFont (Fonts::bodyLabel());
        g.setColour (tokens::muted);

        juce::String status;
        if (lib.isScanning())
            status = "Scanning" + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"))
                       + " " + juce::String (lib.getScanProgress()) + " files";
        else
            status = juce::String (lib.getIndexedCount()) + " samples indexed";
        g.drawText (status, 24, 224, 420, 16, juce::Justification::centredLeft);

        g.drawText ("Suggestions rank these against the selected source type.",
                    24, 242, 560, 14, juce::Justification::centredLeft);

        if (lib.getFolders().isEmpty())
        {
            g.setColour (tokens::muted.withAlpha (0.8f));
            g.drawText ("Add your sample packs to power the rescue suggestions.",
                        24, 120, 420, 16, juce::Justification::centred);
        }
    }

    // --- ListBoxModel -----------------------------------------------------
    int getNumRows() override
    {
        return processor.getLibrary().getFolders().size();
    }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                           bool selected) override
    {
        if (selected)
        {
            g.setColour (tokens::panelHigh);
            g.fillRect (0, 0, width, height);
        }
        const auto folders = processor.getLibrary().getFolders();
        if (row >= folders.size())
            return;

        const juce::File folder (folders[row]);
        if (auto* ic = Assets::icon ("folder", selected ? tokens::cyan : tokens::muted))
            ic->drawWithin (g, juce::Rectangle<float> (8.0f, (float) height * 0.5f - 8.0f,
                                                       16.0f, 16.0f),
                            juce::RectanglePlacement::centred, 1.0f);
        g.setFont (Fonts::bodyValue());
        g.setColour (selected ? tokens::white : tokens::text);
        g.drawText (folder.getFileName(), 32, 0, 200, height, juce::Justification::centredLeft);
        g.setFont (Fonts::make (10.0f));
        g.setColour (tokens::muted);
        g.drawText (folder.getFullPathName(), 236, 0, width - 244, height,
                    juce::Justification::centredLeft);
    }

private:
    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> (
            "Add a sample folder",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory));

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<LibraryViewComponent> (this)]
                              (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && fc.getResult().isDirectory())
                                      safe->processor.getLibrary().addFolder (fc.getResult());
                              });
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        folderList.updateContent();
        repaint();
    }

    void timerCallback() override
    {
        if (processor.getLibrary().isScanning() && (isShowing() || headlessRefreshMode()))
            repaint (24, 224, 420, 16);
    }

    SourceGloProcessor& processor;
    juce::ListBox folderList { "Library folders" };
    AssetButton addButton    { "Add Folder", ButtonKind::smallCyan, "ADD FOLDER", "folder" };
    AssetButton removeButton { "Remove Folder", ButtonKind::smallGold, "REMOVE" };
    AssetButton rescanButton { "Rescan", ButtonKind::smallCyan, "RESCAN" };
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace sourceglo
