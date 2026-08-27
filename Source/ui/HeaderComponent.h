/*
    HeaderComponent - premium logo, preset navigation, save/undo utilities,
    settings, help and power. Bounds: layout::header {6,4,1479,64}; all child
    coordinates below are header-local (design px minus the header origin).
*/

#pragma once
#include "Widgets.h"
#include "ListOverlay.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class HeaderComponent : public juce::Component,
                        private juce::ChangeListener
{
public:
    explicit HeaderComponent (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Header");
        processor.getPresets().changed.addChangeListener (this);

        auto initIcon = [this] (IconButton& b, const juce::String& tip)
        {
            b.setTooltip (tip);
            addAndMakeVisible (b);
        };

        prevButton.setTooltip ("Previous preset");
        nextButton.setTooltip ("Next preset");
        prevButton.setBoxed (true);
        nextButton.setBoxed (true);
        prevButton.setIconPadding (12.0f);
        nextButton.setIconPadding (12.0f);
        addAndMakeVisible (prevButton);
        addAndMakeVisible (nextButton);
        prevButton.onClick = [this] { processor.getPresets().step (-1); };
        nextButton.onClick = [this] { processor.getPresets().step (1); };

        saveButton.setLeftAligned (true);
        saveAsButton.setLeftAligned (true);
        saveButton.setIconPadding (6.0f);
        saveAsButton.setIconPadding (6.0f);
        initIcon (saveButton,     "Save preset");
        initIcon (saveAsButton,   "Save preset as...");
        initIcon (undoButton,     "Undo");
        initIcon (redoButton,     "Redo");
        initIcon (settingsButton, "Settings");
        initIcon (helpButton,     "Help");
        initIcon (powerButton,    "Power / bypass");

        undoButton.onClick = [this] { processor.getUndoManager().undo(); };
        redoButton.onClick = [this] { processor.getUndoManager().redo(); };
        saveButton.onClick = [this]
        {
            auto& bank = processor.getPresets();
            if (! bank.currentIsFactory() && bank.overwriteCurrent())
                return;
            promptSaveAs();                     // factory presets save as new
        };
        saveAsButton.onClick = [this] { promptSaveAs(); };

        settingsButton.onClick = [this] { showSettingsMenu(); };
        helpButton.onClick = [this] { showAbout(); };

        powerButton.setClickingTogglesState (false);
        powerButton.onClick = [this]
        {
            if (auto* param = processor.getAPVTS().getParameter (pid::bypass))
                param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
            repaint();
        };

        onScalePreset = [] (float) {};
    }

    ~HeaderComponent() override
    {
        processor.getPresets().changed.removeChangeListener (this);
    }

    // Set by the editor: opens the in-window chooser. A plugin's list must
    // stay inside the plugin window.
    std::function<void (juce::Rectangle<int>, juce::String,
                        std::vector<ListOverlay::Item>, std::function<void (int)>)> openList;

    // Headless tools: open the real preset browser for the film.
    void openPresetBrowser()  { showPresetMenu(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (juce::Rectangle<int> (784, 14, 222, 40).contains (e.getPosition()))
            showPresetMenu();
    }

    std::function<void (float)> onScalePreset;   // wired by the editor

    void resized() override
    {
        // The mockup places the preset cluster ~28 px right of the layout
        // JSON's rects; the reference image is the visual authority.
        prevButton.setBounds (772, 14, 42, 40);
        nextButton.setBounds (1032, 14, 42, 40);

        saveButton.setBounds     (1098, 21, 72, 30);
        saveAsButton.setBounds   (1176, 21, 84, 30);
        undoButton.setBounds     (1258, 21, 30, 30);
        redoButton.setBounds     (1288, 21, 30, 30);
        settingsButton.setBounds (1345, 20, 32, 32);
        helpButton.setBounds     (1390, 20, 32, 32);
        powerButton.setBounds    (1435, 20, 32, 32);
    }

    void paint (juce::Graphics& g) override
    {
        // Premium logo, exactly {19,16,320,42} on the base canvas
        // (header-local 13,12). LOGO_USAGE_GUIDE.md.
        const juce::Rectangle<float> logoRect (13.0f, 12.0f, 320.0f, 42.0f);
        auto logo = Assets::logoHeader (currentScale);
        if (logo.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (logo, logoRect, juce::RectanglePlacement::stretchToFit);
        }

        // Preset box (the prev/next buttons are children).
        const juce::Rectangle<float> box (812.0f, 14.0f, 222.0f, 40.0f);
        g.setColour (tokens::bg1);
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (tokens::stroke);
        g.drawRoundedRectangle (box.reduced (0.5f), 6.0f, 1.0f);
        auto& bank = processor.getPresets();
        g.setColour (tokens::white);
        g.setFont (Fonts::bodyValue().withHeight (15.0f));
        g.drawText (bank.getCurrentName() + (bank.isModified() ? " *" : ""),
                    box.toNearestInt(), juce::Justification::centred);

        // SAVE / SAVE AS get text beside their icons.
        drawIconLabel (g, saveButton,   "SAVE");
        drawIconLabel (g, saveAsButton, "SAVE AS");

        // Power ring: lit cyan while the plugin is active.
        const bool bypassed = processor.getAPVTS().getRawParameterValue (pid::bypass)->load() > 0.5f;
        if (! bypassed)
        {
            auto r = powerButton.getBounds().toFloat().expanded (2.0f);
            g.setColour (tokens::cyan.withAlpha (0.18f));
            g.fillEllipse (r);
        }
    }

    void setCurrentScale (float s)  { currentScale = s; }

private:
    void drawIconLabel (juce::Graphics& g, const IconButton& b, const juce::String& text)
    {
        g.setColour (tokens::text);
        g.setFont (Fonts::make (12.0f, false, true).withExtraKerningFactor (0.05f));
        auto r = b.getBounds();
        g.drawText (text, r.getX() + 26, r.getY(), r.getWidth() - 26, r.getHeight(),
                    juce::Justification::centredLeft);
    }

    void showPresetMenu()
    {
        if (! openList)
            return;

        auto& bank = processor.getPresets();
        std::vector<ListOverlay::Item> list;
        juce::String lastCategory;
        for (int i = 0; i < bank.getNumPresets(); ++i)
        {
            const auto& preset = bank.getPreset (i);
            ListOverlay::Item item;
            item.text = preset.name;
            if (preset.category != lastCategory)
            {
                lastCategory = preset.category;
                item.section = lastCategory;
            }
            item.ticked = (i == bank.getCurrentIndex());
            list.push_back (std::move (item));
        }

        openList (localAreaToGlobalOfParent ({ 784, 14, 222, 40 }), "Presets", std::move (list),
                  [safe = juce::Component::SafePointer<HeaderComponent> (this)] (int index)
                  {
                      if (safe != nullptr)
                          safe->processor.getPresets().load (index);
                  });
    }

    // The overlay lives in the editor's canvas, so anchors are converted out
    // of this component's local space into the parent's.
    juce::Rectangle<int> localAreaToGlobalOfParent (juce::Rectangle<int> local) const
    {
        return local + getPosition();
    }

    void promptSaveAs()
    {
        auto* window = new juce::AlertWindow ("Save Preset",
                                              "Name for this preset:",
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", processor.getPresets().getCurrentName());
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        window->enterModalState (true,
            juce::ModalCallbackFunction::create (
                [safe = juce::Component::SafePointer<HeaderComponent> (this), window] (int r)
                {
                    if (safe != nullptr && r == 1)
                        safe->processor.getPresets().saveUserPreset (
                            window->getTextEditorContents ("name"));
                }),
            true);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        repaint();
    }

    void showSettingsMenu()
    {
        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());
        m.addSectionHeader ("UI Scale");
        const int scales[] = { 70, 80, 90, 100, 110, 125, 150 };
        for (int s : scales)
            m.addItem (s, juce::String (s) + " %", true,
                       std::abs (currentScale - (float) s / 100.0f) < 0.02f);

        m.showMenuAsync (juce::PopupMenu::Options()
                             .withParentComponent (getTopLevelComponent())
                            .withTargetComponent (&settingsButton),
                         [safe = juce::Component::SafePointer<HeaderComponent> (this)] (int r)
                         {
                             if (safe != nullptr && r > 0)
                                 safe->onScalePreset ((float) r / 100.0f);
                         });
    }

    void showAbout()
    {
        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());
        m.addSectionHeader (juce::String ("SourceGlo Pro ") + JucePlugin_VersionString);
        m.addItem (1, "Production Intelligence for Better Mixes", false);
        m.addItem (2, "Diamond Loopz", false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&helpButton));
    }

    SourceGloProcessor& processor;

    IconButton prevButton   { "Previous preset", "chevron_left" };
    IconButton nextButton   { "Next preset", "chevron_right" };
    IconButton saveButton   { "Save", "save" };
    IconButton saveAsButton { "Save As", "save_as" };
    IconButton undoButton   { "Undo", "undo" };
    IconButton redoButton   { "Redo", "redo" };
    IconButton settingsButton { "Settings", "settings" };
    IconButton helpButton   { "Help", "help" };
    IconButton powerButton  { "Power", "power", tokens::cyan, tokens::cyan };

    float currentScale = 1.0f;
};

} // namespace sourceglo
