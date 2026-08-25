/*
    AnalysisTabsComponent - the lower main panel: tab bar plus the content
    region. Bounds: layout::lowerMainPanel {253,528,760,441} minus the macro
    strip = {253,528,760,314}; coordinates are local. The Analyze tab hosts
    the spectrum and masking/fit analyzers; the other four tabs present
    styled placeholder views until their milestones land. Switching tabs
    never moves or resizes the panel.
*/

#pragma once
#include "Widgets.h"
#include "SpectrumAnalyzerComponent.h"
#include "MaskingFitComponent.h"
#include "LibraryViewComponent.h"
#include "FitTabView.h"
#include "RescueTabView.h"
#include "DetailTabView.h"

namespace sourceglo
{

class AnalysisTabsComponent : public juce::Component
{
public:
    explicit AnalysisTabsComponent (SourceGloProcessor& p)
        : spectrum (p), maskingFit (p), libraryView (p),
          fitView (p), rescueView (p), detailView (p)
    {
        setTitle ("Analysis views");
        addAndMakeVisible (spectrum);
        addAndMakeVisible (maskingFit);
        addChildComponent (libraryView);
        addChildComponent (fitView);
        addChildComponent (rescueView);
        addChildComponent (detailView);
    }

    void selectTab (int index)
    {
        index = juce::jlimit (0, numTabs - 1, index);
        if (index == activeTab)
            return;
        activeTab = index;
        applyTabVisibility();
        repaint();
    }

    void resized() override
    {
        // Design-global {262,574,395,263} / {665,574,339,263} -> local -253,-528.
        // 300 tall, not 263: the mockup's frequency labels sit just below
        // the panel edge, over the shell's divider zone.
        spectrum.setBounds (9, 46, 412, 300);
        maskingFit.setBounds (412, 46, 339, 263);

        const juce::Rectangle<int> content (9, 46, 742, 263);
        libraryView.setBounds (content);
        fitView.setBounds (content);
        rescueView.setBounds (content);
        detailView.setBounds (content);
    }

    void paint (juce::Graphics& g) override
    {
        // --- tab bar: 5 tabs across the top 41 px.
        for (int i = 0; i < numTabs; ++i)
        {
            const auto r = tabBounds (i);
            const bool active = i == activeTab;

            if (active)
            {
                auto art = Assets::tabActive();
                if (art.isValid())
                    g.drawImage (art, r.toFloat(), juce::RectanglePlacement::stretchToFit);

                g.setColour (tokens::cyan);
                g.fillRect (r.getX() + 10, r.getBottom() - 3, r.getWidth() - 20, 3);
            }

            g.setFont (Fonts::tab());
            g.setColour (active ? tokens::cyan
                                : (i == hoverTab ? tokens::text : tokens::muted));
            g.drawText (tabNames[i], r, juce::Justification::centred);
        }

    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int idx = tabIndexAt (e.getPosition());
        if (idx != hoverTab) { hoverTab = idx; repaint (0, 0, getWidth(), 41); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverTab != -1) { hoverTab = -1; repaint (0, 0, getWidth(), 41); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int idx = tabIndexAt (e.getPosition());
        if (idx >= 0)
            selectTab (idx);
    }

private:
    static constexpr int numTabs = 5;
    static constexpr const char* tabNames[numTabs] =
        { "ANALYZE", "FIT", "RESCUE", "DETAIL", "LIBRARY" };

    juce::Rectangle<int> tabBounds (int i) const
    {
        // Five tabs, 137 px each, from local x 9 (design-global 262..947).
        return { 9 + i * 137, 0, 137, 41 };
    }

    int tabIndexAt (juce::Point<int> pos) const
    {
        for (int i = 0; i < numTabs; ++i)
            if (tabBounds (i).contains (pos))
                return i;
        return -1;
    }

    void applyTabVisibility()
    {
        spectrum.setVisible (activeTab == 0);
        maskingFit.setVisible (activeTab == 0);
        fitView.setVisible (activeTab == 1);
        rescueView.setVisible (activeTab == 2);
        detailView.setVisible (activeTab == 3);
        libraryView.setVisible (activeTab == 4);
    }

    SpectrumAnalyzerComponent spectrum;
    MaskingFitComponent maskingFit;
    LibraryViewComponent libraryView;
    FitTabView fitView;
    RescueTabView rescueView;
    DetailTabView detailView;

    int activeTab = 0, hoverTab = -1;
};

} // namespace sourceglo
