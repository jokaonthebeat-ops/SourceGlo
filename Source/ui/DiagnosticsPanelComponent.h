/*
    DiagnosticsPanelComponent - four data-driven diagnostic cards on the
    supplied card art. Bounds: layout::diagnosticsPanel {1037,75,444,446};
    coordinates are panel-local.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

class DiagnosticsPanelComponent : public juce::Component
{
public:
    explicit DiagnosticsPanelComponent (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Diagnostics");
        cornerButton.setTooltip ("Diagnostics options");
        cornerButton.setIconPadding (5.0f);
        addAndMakeVisible (cornerButton);
    }

    void resized() override
    {
        cornerButton.setBounds (394, 23, 26, 26);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (Fonts::panelTitle());
        g.setColour (tokens::white);
        g.drawText ("DIAGNOSTICS", 56, 24, 250, 20, juce::Justification::centredLeft);

        const auto& diags = processor.getAnalysis().diagnostics;
        for (int i = 0; i < (int) diags.size() && i < 4; ++i)
            drawCard (g, { 32, 66 + i * 98, 380, 84 }, diags[(size_t) i], i == hoverIndex);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int idx = cardIndexAt (e.getPosition());
        if (idx != hoverIndex)
        {
            hoverIndex = idx;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverIndex != -1) { hoverIndex = -1; repaint(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Disclosure is a later milestone; give the press visible feedback now.
        pressedIndex = cardIndexAt (e.getPosition());
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        pressedIndex = -1;
        repaint();
    }

private:
    int cardIndexAt (juce::Point<int> pos) const
    {
        for (int i = 0; i < 4; ++i)
            if (juce::Rectangle<int> (32, 66 + i * 98, 380, 84).contains (pos))
                return i;
        return -1;
    }

    static juce::Colour severityColour (Severity s)
    {
        switch (s)
        {
            case Severity::high:   return tokens::red;
            case Severity::medium: return tokens::amber;
            case Severity::good:
            default:               return tokens::green;
        }
    }

    static const char* severityText (Severity s)
    {
        switch (s)
        {
            case Severity::high:   return "HIGH";
            case Severity::medium: return "MEDIUM";
            case Severity::good:
            default:               return "GOOD";
        }
    }

    void drawCard (juce::Graphics& g, juce::Rectangle<int> r,
                   const Diagnostic& d, bool hovered)
    {
        auto art = Assets::diagnosticCard ((int) d.severity);
        if (art.isValid())
        {
            g.setOpacity (hovered ? 1.0f : 0.96f);
            g.drawImage (art, r.toFloat(), juce::RectanglePlacement::stretchToFit);
            g.setOpacity (1.0f);
        }
        else
        {
            g.setColour (tokens::panelAlt);
            g.fillRoundedRectangle (r.toFloat(), 8.0f);
            g.setColour (severityColour (d.severity).withAlpha (0.6f));
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        }

        const auto accent = severityColour (d.severity);

        // State icon in its rounded well.
        const juce::Rectangle<int> iconBox (r.getX() + 26, r.getY() + 19, 44, 44);
        g.setColour (tokens::bg1.withAlpha (0.85f));
        g.fillRoundedRectangle (iconBox.toFloat(), 8.0f);
        g.setColour (accent.withAlpha (0.25f));
        g.drawRoundedRectangle (iconBox.toFloat().reduced (0.5f), 8.0f, 1.0f);
        if (auto* ic = Assets::icon (d.icon, accent))
            ic->drawWithin (g, iconBox.toFloat().reduced (7.0f),
                            juce::RectanglePlacement::centred, 1.0f);

        // Title + body.
        g.setFont (Fonts::diagTitle());
        g.setColour (tokens::white);
        g.drawText (d.title, r.getX() + 94, r.getY() + 13, 220, 17,
                    juce::Justification::centredLeft);

        g.setFont (Fonts::diagBody());
        g.setColour (tokens::bodyLabel);
        auto lines = juce::StringArray::fromLines (d.body);
        for (int i = 0; i < lines.size() && i < 2; ++i)
            g.drawText (lines[i], r.getX() + 94, r.getY() + 34 + i * 15, 216, 14,
                        juce::Justification::centredLeft);

        // Severity badge.
        const juce::Rectangle<float> badge ((float) r.getX() + 317.0f, (float) r.getY() + 32.0f,
                                            48.0f, 20.0f);
        g.setColour (accent.withAlpha (0.14f));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (accent);
        g.drawRoundedRectangle (badge.reduced (0.5f), 4.0f, 1.0f);
        g.setFont (Fonts::make (9.5f, false, true).withExtraKerningFactor (0.06f));
        g.drawText (severityText (d.severity), badge.toNearestInt(),
                    juce::Justification::centred);

        // Disclosure chevron.
        juce::Path p;
        const float cx = (float) r.getX() + 371.0f, cy = (float) r.getCentreY();
        p.startNewSubPath (cx - 2.5f, cy - 5.0f);
        p.lineTo (cx + 2.5f, cy);
        p.lineTo (cx - 2.5f, cy + 5.0f);
        g.setColour (hovered ? tokens::text : tokens::muted);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    SourceGloProcessor& processor;
    IconButton cornerButton { "Diagnostics options", "menu" };
    int hoverIndex = -1, pressedIndex = -1;
};

} // namespace sourceglo
