#include "PluginEditor.h"

namespace sourceglo
{

// -----------------------------------------------------------------------------
//  DebugOverlay - development aid required by the build prompt: component
//  bounds, base coordinates, current scale, repaint activity and FPS.
//  Hidden; toggled with Cmd/Ctrl+Shift+D.
// -----------------------------------------------------------------------------
class SourceGloEditor::DebugOverlay : public juce::Component, private juce::Timer
{
public:
    DebugOverlay()
    {
        setInterceptsMouseClicks (false, false);
        setVisible (false);
        startTimerHz (4);
    }

    float scaleToShow = 1.0f;

    void paint (juce::Graphics& g) override
    {
        ++frameCount;

        // Base-coordinate grid every 100 px.
        g.setColour (juce::Colours::magenta.withAlpha (0.10f));
        for (int x = 0; x < Design::width; x += 100)
            g.fillRect (x, 0, 1, Design::height);
        for (int y = 0; y < Design::height; y += 100)
            g.fillRect (0, y, Design::width, 1);

        // Sibling bounds, recursively.
        if (auto* parent = getParentComponent())
            for (auto* sibling : parent->getChildren())
                if (sibling != this && sibling->isVisible())
                    drawBounds (g, *sibling, sibling->getBounds(), 0);

        g.setColour (juce::Colours::magenta);
        g.setFont (Fonts::make (13.0f, false, true));
        g.drawText ("DEBUG  scale " + juce::String (scaleToShow, 3)
                      + "   base 1491x1055   ~" + juce::String (fps) + " fps",
                    12, Design::height - 26, 600, 18, juce::Justification::centredLeft);
    }

private:
    void drawBounds (juce::Graphics& g, juce::Component& c,
                     juce::Rectangle<int> r, int depth)
    {
        g.setColour (juce::Colours::magenta.withAlpha (depth == 0 ? 0.75f : 0.35f));
        g.drawRect (r, 1);

        if (depth == 0)
        {
            g.setFont (Fonts::make (9.0f));
            g.drawText (c.getName().isNotEmpty() ? c.getName() : c.getTitle(),
                        r.getX() + 3, r.getY() + 1, 220, 11, juce::Justification::centredLeft);
        }

        for (auto* child : c.getChildren())
            if (child->isVisible())
                drawBounds (g, *child,
                            child->getBounds().translated (r.getX(), r.getY()), depth + 1);
    }

    void timerCallback() override
    {
        if (! isVisible())
            return;
        fps = frameCount * 4;
        frameCount = 0;
        repaint();
    }

    int frameCount = 0, fps = 0;
};

// -----------------------------------------------------------------------------
//  ContentComponent - the fixed 1491 x 1055 design canvas: shell + panels.
// -----------------------------------------------------------------------------
class SourceGloEditor::ContentComponent : public juce::Component
{
public:
    explicit ContentComponent (SourceGloProcessor& p)
        : header (p), sourcePanel (p), hud (p), diagnostics (p),
          analysisTabs (p), macroBank (p), rescue (p), footer (p)
    {
        setOpaque (true);
        addAndMakeVisible (header);
        addAndMakeVisible (sourcePanel);
        addAndMakeVisible (hud);
        addAndMakeVisible (diagnostics);
        addAndMakeVisible (analysisTabs);
        addAndMakeVisible (macroBank);
        addAndMakeVisible (rescue);
        addAndMakeVisible (footer);
        addChildComponent (debugOverlay);   // hidden until Cmd+Shift+D
        setSize (Design::width, Design::height);
    }

    void resized() override
    {
        header.setBounds (layout::header);
        sourcePanel.setBounds (layout::sourcePanel);
        hud.setBounds (layout::heroPanel);
        diagnostics.setBounds (layout::diagnosticsPanel);
        analysisTabs.setBounds (layout::lowerMainPanel.withHeight (332));
        macroBank.setBounds (layout::macroStrip);
        rescue.setBounds (layout::rescuePanel);
        footer.setBounds (layout::footer);
        debugOverlay.setBounds (getLocalBounds());
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (tokens::bg0);

        // The 2x shell keeps edges crisp above 100 %.
        auto shell = displayScale > 1.02f ? Assets::shell2x() : Assets::shell();
        if (! shell.isValid())
            shell = Assets::shell();

        if (shell.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (shell, getLocalBounds().toFloat(),
                         juce::RectanglePlacement::stretchToFit);
        }
    }

    HeaderComponent header;
    SourcePanelComponent sourcePanel;
    SourceScoreHUD hud;
    DiagnosticsPanelComponent diagnostics;
    AnalysisTabsComponent analysisTabs;
    MacroBankComponent macroBank;
    RescueSuggestionsComponent rescue;
    FooterStatusComponent footer;
    DebugOverlay debugOverlay;
    float displayScale = 1.0f;
};

// -----------------------------------------------------------------------------
//  Editor
// -----------------------------------------------------------------------------
SourceGloEditor::SourceGloEditor (SourceGloProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    content = std::make_unique<ContentComponent> (processor);
    addAndMakeVisible (*content);

    content->footer.onScaleChanged = [this] (float s)
    {
        s = juce::jlimit (0.7f, 1.5f, s);
        setSize (juce::roundToInt (Design::width * s),
                 juce::roundToInt (Design::height * s));
    };
    content->header.onScalePreset = content->footer.onScaleChanged;

    setWantsKeyboardFocus (true);

    setResizable (true, true);
    if (auto* constrainer = getConstrainer())
    {
        constrainer->setFixedAspectRatio (Design::aspect);
        constrainer->setSizeLimits (Design::minWidth, Design::minHeight,
                                    Design::maxWidth, Design::maxHeight);
    }

    const float saved = juce::jlimit (0.7f, 1.5f, processor.getSavedUIScale());
    setSize (juce::roundToInt (Design::width * saved),
             juce::roundToInt (Design::height * saved));
}

SourceGloEditor::~SourceGloEditor()
{
    setLookAndFeel (nullptr);
}

void SourceGloEditor::resized()
{
    if (content == nullptr)
        return;

    const auto area = getLocalBounds().toFloat();
    const float scale = juce::jmin (area.getWidth() / (float) Design::width,
                                    area.getHeight() / (float) Design::height);

    // Centre the scaled design when the host supplies extra room.
    const float ox = (area.getWidth()  - Design::width  * scale) * 0.5f;
    const float oy = (area.getHeight() - Design::height * scale) * 0.5f;

    content->setBounds (0, 0, Design::width, Design::height);
    content->setTransform (juce::AffineTransform::scale (scale).translated (ox, oy));

    currentScale = scale;
    content->displayScale = scale;
    content->header.setCurrentScale (scale);
    content->footer.setDisplayedScale (scale);
    content->debugOverlay.scaleToShow = scale;

    processor.setSavedUIScale (scale);
}

void SourceGloEditor::paint (juce::Graphics& g)
{
    g.fillAll (tokens::bg0);   // letterbox behind the centred design
}

bool SourceGloEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == 'D'
         && key.getModifiers().isCommandDown()
         && key.getModifiers().isShiftDown())
    {
        auto& overlay = content->debugOverlay;
        overlay.setVisible (! overlay.isVisible());
        return true;
    }
    return false;
}

void SourceGloEditor::refreshDisplays()
{
    headlessRefreshMode() = true;
    juce::Timer::callPendingTimersSynchronously();
    content->repaint();
}

} // namespace sourceglo
