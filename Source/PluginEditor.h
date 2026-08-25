/*
    SourceGloEditor - the approved 1491 x 1055 interface.

    One fixed-size content component holds every panel in design coordinates;
    the editor applies a single uniform AffineTransform scale and centres the
    result (JUCE_IMPLEMENTATION_SPEC section 1: never stretch X and Y
    independently). Aspect ratio is locked 1491:1055, minimum 1044 x 739.
*/

#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/HeaderComponent.h"
#include "ui/SourcePanelComponent.h"
#include "ui/SourceScoreHUD.h"
#include "ui/DiagnosticsPanelComponent.h"
#include "ui/AnalysisTabsComponent.h"
#include "ui/MacroBankComponent.h"
#include "ui/RescueSuggestionsComponent.h"
#include "ui/FooterStatusComponent.h"

namespace sourceglo
{

class SourceGloEditor : public juce::AudioProcessorEditor
{
public:
    explicit SourceGloEditor (SourceGloProcessor&);
    ~SourceGloEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Drives every display timer once, synchronously - used by make uishot.
    void refreshDisplays();

    // Headless tools: switch the lower panel's tab for a screenshot.
    void showAnalysisTab (int index);

private:
    class ContentComponent;
    class DebugOverlay;

    SourceGloProcessor& processor;
    SourceGloLookAndFeel lookAndFeel;
    std::unique_ptr<ContentComponent> content;
    juce::TooltipWindow tooltips { this, 650 };
    float currentScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SourceGloEditor)
};

} // namespace sourceglo
