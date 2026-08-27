/*
    ListOverlay - an in-window chooser for the preset browser and the
    source-type list.

    juce::PopupMenu opens an OS window. In a plugin that means the list can
    land outside the plugin's own window (fullscreen DAWs are the usual
    offender), it cannot be styled with the product's own palette, and it
    cannot be captured by an offline render of the editor at all - which is
    why the preset library never appeared in the demo film.

    This is a plain child component instead: it covers the design canvas,
    dims what is behind it, and draws a panel anchored to whatever opened it.
*/

#pragma once
#include "Theme.h"
#include "../Assets.h"

namespace sourceglo
{

class ListOverlay : public juce::Component
{
public:
    struct Item
    {
        juce::String text;
        juce::String section;      // empty continues the previous section
        bool ticked = false;
    };

    ListOverlay()
    {
        setTitle ("Chooser");
        setInterceptsMouseClicks (true, true);
        setVisible (false);
    }

    void show (juce::Rectangle<int> anchorInParent, juce::String heading,
               std::vector<Item> newItems, std::function<void (int)> pick)
    {
        items = std::move (newItems);
        onPick = std::move (pick);
        title = std::move (heading);
        anchor = anchorInParent;
        hoverIndex = -1;
        scroll = 0;

        for (int i = 0; i < (int) items.size(); ++i)
            if (items[(size_t) i].ticked)
                scroll = juce::jmax (0, juce::jmin (maxScroll(), i - 4));

        setVisible (true);
        toFront (false);
        repaint();
    }

    void hide()
    {
        if (! isVisible())
            return;
        setVisible (false);
        onPick = nullptr;
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int idx = indexAt (e.getPosition());
        if (idx != hoverIndex) { hoverIndex = idx; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverIndex != -1) { hoverIndex = -1; repaint(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int idx = indexAt (e.getPosition());
        auto pick = onPick;
        if (idx >= 0)
        {
            hide();
            if (pick) pick (idx);
        }
        else if (! panelBounds().contains (e.getPosition()))
        {
            hide();     // a click outside dismisses, like a menu would
        }
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const int next = juce::jlimit (0, maxScroll(),
                                       scroll - juce::roundToInt (w.deltaY * 6.0f));
        if (next != scroll) { scroll = next; repaint(); }
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { hide(); return true; }
        return false;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.45f));

        const auto panel = panelBounds().toFloat();

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (panel.translated (0.0f, 3.0f).expanded (2.0f), 10.0f);
        g.setColour (tokens::panel);
        g.fillRoundedRectangle (panel, 9.0f);
        g.setColour (tokens::cyan.withAlpha (0.55f));
        g.drawRoundedRectangle (panel.reduced (0.5f), 9.0f, 1.2f);

        g.setFont (Fonts::make (11.0f, false, true).withExtraKerningFactor (0.06f));
        g.setColour (tokens::muted);
        g.drawText (title.toUpperCase(), (int) panel.getX() + 14, (int) panel.getY() + 9,
                    (int) panel.getWidth() - 28, 16, juce::Justification::centredLeft);

        g.setColour (tokens::strokeSoft);
        g.fillRect (panel.getX() + 10.0f, panel.getY() + 29.0f, panel.getWidth() - 20.0f, 1.0f);

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (listBounds());

        juce::String lastSection;
        int y = listBounds().getY() - scroll * rowHeight;

        for (int i = 0; i < (int) items.size(); ++i)
        {
            const auto& item = items[(size_t) i];

            if (item.section.isNotEmpty() && item.section != lastSection)
            {
                lastSection = item.section;
                g.setFont (Fonts::make (9.5f, false, true).withExtraKerningFactor (0.08f));
                g.setColour (tokens::cyanDark);
                g.drawText (item.section.toUpperCase(), (int) panel.getX() + 16, y + 3,
                            (int) panel.getWidth() - 32, 14, juce::Justification::centredLeft);
                y += sectionHeight;
            }

            const juce::Rectangle<int> row ((int) panel.getX() + 6, y,
                                            (int) panel.getWidth() - 12, rowHeight);
            if (row.getBottom() > listBounds().getY() && row.getY() < listBounds().getBottom())
            {
                if (i == hoverIndex)
                {
                    g.setColour (tokens::panelHigh);
                    g.fillRoundedRectangle (row.toFloat(), 4.0f);
                }
                if (item.ticked)
                {
                    g.setColour (tokens::cyan);
                    g.fillRoundedRectangle ((float) row.getX() + 2.0f,
                                            (float) row.getCentreY() - 6.0f, 2.5f, 12.0f, 1.2f);
                }

                g.setFont (Fonts::bodyValue().withHeight (13.0f));
                g.setColour (item.ticked ? tokens::white : tokens::text);
                g.drawText (item.text, row.getX() + 14, row.getY(),
                            row.getWidth() - 22, row.getHeight(),
                            juce::Justification::centredLeft, true);
            }
            y += rowHeight;
        }

        if (maxScroll() > 0)
        {
            const auto lb = listBounds().toFloat();
            const float frac = (float) scroll / (float) maxScroll();
            const float barH = juce::jmax (24.0f, lb.getHeight() * 0.25f);
            g.setColour (tokens::stroke);
            g.fillRoundedRectangle (lb.getRight() - 4.0f,
                                    lb.getY() + frac * (lb.getHeight() - barH),
                                    3.0f, barH, 1.5f);
        }
    }

private:
    static constexpr int rowHeight = 24;
    static constexpr int sectionHeight = 20;

    int visibleRows() const   { return juce::jmax (4, listBounds().getHeight() / rowHeight); }

    int contentRows() const
    {
        int sections = 0;
        juce::String last;
        for (const auto& i : items)
            if (i.section.isNotEmpty() && i.section != last) { last = i.section; ++sections; }
        return (int) items.size() + sections;
    }

    int maxScroll() const  { return juce::jmax (0, contentRows() - visibleRows()); }

    juce::Rectangle<int> panelBounds() const
    {
        const int w = juce::jmax (210, anchor.getWidth() + 40);
        const int h = juce::jmin (430, 40 + contentRows() * rowHeight);
        int x = anchor.getCentreX() - w / 2;
        int y = anchor.getBottom() + 6;

        if (y + h > getHeight() - 8)
            y = juce::jmax (8, anchor.getY() - h - 6);
        x = juce::jlimit (8, juce::jmax (8, getWidth() - w - 8), x);
        return { x, y, w, h };
    }

    juce::Rectangle<int> listBounds() const
    {
        return panelBounds().withTrimmedTop (34).reduced (4, 6);
    }

    int indexAt (juce::Point<int> p) const
    {
        if (! listBounds().contains (p))
            return -1;

        juce::String lastSection;
        int y = listBounds().getY() - scroll * rowHeight;
        for (int i = 0; i < (int) items.size(); ++i)
        {
            const auto& item = items[(size_t) i];
            if (item.section.isNotEmpty() && item.section != lastSection)
            {
                lastSection = item.section;
                y += sectionHeight;
            }
            if (p.getY() >= y && p.getY() < y + rowHeight)
                return i;
            y += rowHeight;
        }
        return -1;
    }

    std::vector<Item> items;
    std::function<void (int)> onPick;
    juce::String title;
    juce::Rectangle<int> anchor;
    int hoverIndex = -1, scroll = 0;
};

} // namespace sourceglo
