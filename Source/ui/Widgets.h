/*
    Widgets.h - the small controls every panel shares.

    All of them draw the supplied artwork (Assets) and fall back to flat
    token-coloured shapes only when a file failed to load - visibly, so a
    missing asset reads as the load problem it is.
*/

#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include "../Assets.h"

namespace sourceglo
{

// -----------------------------------------------------------------------------
//  AssetButton - normal/hover/down/disabled from the supplied button art,
//  with an optional icon + uppercase label drawn on top.
// -----------------------------------------------------------------------------
class AssetButton : public juce::Button
{
public:
    AssetButton (const juce::String& name, ButtonKind kindIn,
                 const juce::String& labelIn = {}, const juce::String& iconIn = {})
        : juce::Button (name), kind (kindIn), label (labelIn), iconName (iconIn)
    {
        setTitle (name);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setLabel (const juce::String& l)     { label = l; repaint(); }
    void setIconTint (juce::Colour c)         { iconTint = c; repaint(); }
    void setLabelColour (juce::Colour c)      { labelTint = c; repaint(); }
    void setFontHeight (float h)              { fontHeight = h; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        // sourceglo::ButtonState, not the juce::Button member type of the
        // same name that unqualified lookup finds first in a Button subclass.
        // A LATCHED button is not a pressed one. The pack ships no "active"
        // export - measured mean luminance is normal 28.9, down 31.0, hover
        // 35.8 - so "down" for an engaged toggle reads as pushed-in and dim,
        // which is why the engaged Fix Source button never looked lit.
        // Toggled uses the brightest supplied art plus the glow below.
        const bool latched = getToggleState() && isEnabled();
        const auto state = ! isEnabled() ? sourceglo::ButtonState::disabled
                         : down          ? sourceglo::ButtonState::down
                         : (over || latched) ? sourceglo::ButtonState::hover
                                             : sourceglo::ButtonState::normal;

        auto art = Assets::button (kind, state);
        const auto r = getLocalBounds().toFloat();

        if (latched)
            drawLatchedGlow (g, r);

        if (art.isValid())
            g.drawImage (art, r, juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (tokens::panelHigh);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (tokens::stroke);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        }

        auto content = getLocalBounds();
        const auto font = Fonts::buttonLabel().withHeight (fontHeight);
        const int textW = label.isNotEmpty()
                            ? (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, label)) : 0;
        const int iconSz = juce::jmin (20, getHeight() - 16);
        const int gap    = (iconName.isNotEmpty() && textW > 0) ? 10 : 0;
        const int total  = textW + gap + (iconName.isNotEmpty() ? iconSz : 0);
        int x = content.getCentreX() - total / 2;

        if (auto* ic = iconName.isNotEmpty() ? Assets::icon (iconName, iconTint) : nullptr)
        {
            ic->drawWithin (g, juce::Rectangle<float> ((float) x,
                            (float) (content.getCentreY() - iconSz / 2),
                            (float) iconSz, (float) iconSz),
                            juce::RectanglePlacement::centred,
                            isEnabled() ? 1.0f : 0.4f);
            x += iconSz + gap;
        }

        if (label.isNotEmpty())
        {
            g.setColour (latched ? tokens::white
                                 : labelTint.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
            g.setFont (font);
            g.drawText (label, x, content.getY(), textW + 4, content.getHeight(),
                        juce::Justification::centredLeft);
        }

        if (latched)
        {
            // A rim inside the art's own edge, so the button reads as ON
            // rather than merely hovered.
            g.setColour (latchColour().withAlpha (0.9f));
            g.drawRoundedRectangle (r.reduced (1.5f), 7.0f, 1.6f);
        }
    }

    // Public so a subclass can pulse it (see FixSourceButton).
    void setGlowIntensity (float g01)
    {
        const float clamped = juce::jlimit (0.0f, 1.0f, g01);
        if (std::abs (clamped - glowIntensity) > 0.01f)
        {
            glowIntensity = clamped;
            repaint();
        }
    }

protected:
    juce::Colour latchColour() const
    {
        return (kind == ButtonKind::mainGold || kind == ButtonKind::smallGold)
                 ? tokens::gold : tokens::cyan;
    }

    void drawLatchedGlow (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        // Concentric strokes outside the art: a cheap bloom that survives the
        // button being drawn over it, and reads at video scale.
        const auto colour = latchColour();
        const float strength = 0.55f + 0.45f * glowIntensity;

        // A soft filled halo first, then concentric strokes over it: the fill
        // carries at video scale, the strokes keep the edge defined.
        g.setColour (colour.withAlpha (0.10f * strength));
        g.fillRoundedRectangle (r.expanded (9.0f), 15.0f);

        for (int i = 6; i >= 1; --i)
        {
            const float grow = (float) i * 2.3f;
            g.setColour (colour.withAlpha (strength * (0.11f + 0.05f * (float) (7 - i))
                                             / (float) i));
            g.drawRoundedRectangle (r.expanded (grow), 8.0f + grow, 2.3f);
        }
    }

private:
    float glowIntensity = 0.0f;
    ButtonKind kind;
    juce::String label, iconName;
    juce::Colour iconTint  = tokens::cyan;
    juce::Colour labelTint = tokens::buttonLbl;
    float fontHeight = 14.0f;
};

// -----------------------------------------------------------------------------
//  IconButton - just a tinted SVG with hover/pressed treatment (header
//  utilities, help "?", chevrons, star, play...).
// -----------------------------------------------------------------------------
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, const juce::String& iconIn,
                juce::Colour tintIn = tokens::muted, juce::Colour activeIn = tokens::white)
        : juce::Button (name), iconName (iconIn), tint (tintIn), active (activeIn)
    {
        setTitle (name);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setIconPadding (float p)  { padding = p; repaint(); }
    void setLeftAligned (bool l)   { leftAligned = l; repaint(); }
    void setBoxed (bool b)         { boxed = b; repaint(); }
    void setCircled (bool c)       { circled = c; repaint(); }
    void setTints (juce::Colour normal, juce::Colour activeC) { tint = normal; active = activeC; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const bool lit = over || down || getToggleState();
        auto area = getLocalBounds().toFloat();

        if (boxed)
        {
            g.setColour (tokens::panelHigh);
            g.fillRoundedRectangle (area.reduced (0.5f), 6.0f);
            g.setColour (lit ? tokens::muted : tokens::stroke);
            g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);
        }
        else if (circled)
        {
            g.setColour (lit ? active : tokens::muted.withAlpha (0.8f));
            g.drawEllipse (area.reduced (1.0f), 1.3f);
        }

        if (leftAligned)
            area = area.removeFromLeft (area.getHeight());   // icon square at left, label beside
        if (auto* ic = Assets::icon (iconName, lit ? active : tint))
            ic->drawWithin (g, area.reduced (padding),
                            juce::RectanglePlacement::centred,
                            isEnabled() ? (down ? 0.8f : 1.0f) : 0.35f);
    }

private:
    juce::String iconName;
    juce::Colour tint, active;
    float padding = 3.0f;
    bool leftAligned = false, boxed = false, circled = false;
};

// -----------------------------------------------------------------------------
//  FilmstripKnob - the supplied 128-frame strips, sliced at load.
// -----------------------------------------------------------------------------
class FilmstripKnob : public juce::Slider
{
public:
    explicit FilmstripKnob (bool trimIn, const juce::String& name)
        : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox),
          trim (trimIn)
    {
        setName (name);
        setTitle (name);
        setMouseDragSensitivity (240);          // slow enough for fine control
        setVelocityModeParameters (1.0, 1, 0.06, true);  // shift = fine adjust
        setDoubleClickReturnValue (true, 0.0);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& strip = trim ? Assets::trimKnob() : Assets::macroKnob();
        const auto bounds = getLocalBounds().toFloat();
        const float size  = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const juce::Rectangle<float> target (bounds.getCentreX() - size * 0.5f,
                                             bounds.getCentreY() - size * 0.5f, size, size);

        if (strip.isValid())
        {
            const auto norm = (float) valueToProportionOfLength (getValue());
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (strip.frameFor (norm), target, juce::RectanglePlacement::centred);
        }
        else
        {
            g.setColour (tokens::panelHigh);
            g.fillEllipse (target);
            g.setColour (tokens::stroke);
            g.drawEllipse (target.reduced (1.0f), 1.5f);
        }
    }

private:
    bool trim;
};

// -----------------------------------------------------------------------------
//  VerticalMeter - trough + segment artwork; the UI applies release ballistics.
// -----------------------------------------------------------------------------
class VerticalMeter : public juce::Component
{
public:
    VerticalMeter() { setInterceptsMouseClicks (false, false); }

    // level in dBFS, floor at -60.
    void setLevel (float dB)
    {
        display = juce::jmax (display * 0.86f - 0.3f,
                              juce::jmap (juce::jlimit (-60.0f, 0.0f, dB), -60.0f, 0.0f, 0.0f, 1.0f) * 100.0f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        auto trough = Assets::meterTrough();

        if (trough.isValid())
            g.drawImage (trough, r, juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (tokens::bg1);
            g.fillRoundedRectangle (r, 3.0f);
        }

        // Segments bottom-up: cyan, gold above -6 dB equivalent, red at top.
        const int segH = 7, segGap = 2;
        const auto inner = r.reduced (5.0f, 6.0f);
        const int count = juce::jmax (1, (int) (inner.getHeight() + segGap) / (segH + segGap));
        const int lit   = juce::roundToInt ((display / 100.0f) * (float) count);

        auto segment = Assets::meterSegment();
        for (int i = 0; i < lit && i < count; ++i)
        {
            const float y = inner.getBottom() - (float) ((i + 1) * (segH + segGap) - segGap);
            const juce::Rectangle<float> seg (inner.getX(), y, inner.getWidth(), (float) segH);
            const float frac = (float) i / (float) count;

            if (segment.isValid())
            {
                // NOTE: drawImage uses the current fill colour's alpha as its
                // opacity, so the colour must be reset to opaque first.
                g.setColour (juce::Colours::white);
                g.drawImage (segment, seg, juce::RectanglePlacement::stretchToFit);
            }
            else
            {
                g.setColour (tokens::cyanMid);
                g.fillRect (seg);
            }

            // Hot-zone tint over the art.
            if (frac > 0.92f)      { g.setColour (tokens::red);  g.fillRect (seg.reduced (1.0f)); }
            else if (frac > 0.78f) { g.setColour (tokens::gold); g.fillRect (seg.reduced (1.0f)); }
        }
    }

private:
    float display = 0.0f;
};

// -----------------------------------------------------------------------------
//  TogglePill - the supplied 56x26 on/off toggle art (Auto Match).
// -----------------------------------------------------------------------------
class TogglePill : public juce::Button
{
public:
    explicit TogglePill (const juce::String& name) : juce::Button (name)
    {
        setTitle (name);
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto art = Assets::toggle (getToggleState());
        const auto r = getLocalBounds().toFloat();
        if (art.isValid())
        {
            g.setOpacity (over ? 1.0f : 0.92f);
            g.drawImage (art, r, juce::RectanglePlacement::centred);
        }
        else
        {
            g.setColour (getToggleState() ? tokens::cyanMid : tokens::panelHigh);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        }
    }
};

// -----------------------------------------------------------------------------
//  SmallPill - tiny bordered text button (INV / MONO / Pre / Post / Overlay...).
//  The mockup draws these as thin outlined pills; active state is cyan.
// -----------------------------------------------------------------------------
class SmallPill : public juce::Button
{
public:
    SmallPill (const juce::String& name, const juce::String& textIn,
               bool toggles = true, juce::Colour activeIn = tokens::cyan)
        : juce::Button (name), text (textIn), active (activeIn)
    {
        setTitle (name);
        setClickingTogglesState (toggles);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setActiveFill (bool f)  { activeFill = f; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        const float corner = 4.0f;

        if (on && activeFill)
        {
            g.setColour (active.withAlpha (0.16f));
            g.fillRoundedRectangle (r, corner);
        }
        g.setColour (on ? active : (over || down ? tokens::muted : tokens::stroke));
        g.drawRoundedRectangle (r, corner, 1.0f);

        g.setColour (on ? active : (over ? tokens::text : tokens::muted));
        g.setFont (Fonts::make (11.0f, false, true).withExtraKerningFactor (0.06f));
        g.drawText (text, getLocalBounds(), juce::Justification::centred);
    }

private:
    juce::String text;
    juce::Colour active;
    bool activeFill = true;
};

// -----------------------------------------------------------------------------
//  AssetDropdown - the supplied 210x34 dropdown art with a text + chevron.
//  Backed by a juce::ComboBox-style popup but drawn entirely from the art.
// -----------------------------------------------------------------------------
class AssetDropdown : public juce::Component
{
public:
    AssetDropdown (const juce::String& name, const juce::StringArray& itemsIn,
                   int initialIndex = 0, const juce::String& leadIconIn = {})
        : items (itemsIn), index (initialIndex), leadIcon (leadIconIn)
    {
        setName (name);
        setTitle (name);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
    }

    std::function<void (int)> onChange;

    int getSelectedIndex() const     { return index; }
    void setSelectedIndex (int i, juce::NotificationType notify = juce::sendNotification)
    {
        index = juce::jlimit (0, items.size() - 1, i);
        if (notify != juce::dontSendNotification && onChange)
            onChange (index);
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu m;
        m.setLookAndFeel (&getLookAndFeel());
        for (int i = 0; i < items.size(); ++i)
            m.addItem (i + 1, items[i], true, i == index);

        m.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (this)
                            .withMinimumWidth (getWidth()),
                         [safe = juce::Component::SafePointer<AssetDropdown> (this)] (int result)
                         {
                             if (safe != nullptr && result > 0)
                                 safe->setSelectedIndex (result - 1);
                         });
    }

    void paint (juce::Graphics& g) override
    {
        auto art = Assets::dropdown();
        const auto r = getLocalBounds().toFloat();

        if (art.isValid())
            g.drawImage (art, r, juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (tokens::panelHigh);
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (tokens::stroke);
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        }

        auto content = getLocalBounds().reduced (12, 0);

        if (leadIcon.isNotEmpty())
        {
            if (auto* ic = Assets::icon (leadIcon, tokens::text))
                ic->drawWithin (g, content.removeFromLeft (16).toFloat().reduced (0.0f, (float) getHeight() * 0.5f - 8.0f),
                                juce::RectanglePlacement::centred, 1.0f);
            content.removeFromLeft (8);
        }

        g.setColour (tokens::white);
        g.setFont (Fonts::bodyValue());
        g.drawText (items[index], content.withTrimmedRight (18),
                    juce::Justification::centredLeft, true);

        // chevron
        juce::Path p;
        const float cx = (float) getWidth() - 18.0f, cy = (float) getHeight() * 0.5f;
        p.startNewSubPath (cx - 4.0f, cy - 2.0f);
        p.lineTo (cx, cy + 2.5f);
        p.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (tokens::muted);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

private:
    juce::StringArray items;
    int index = 0;
    juce::String leadIcon;
};

// -----------------------------------------------------------------------------
//  SourceGloLookAndFeel - popup menus and tooltips in the house style.
// -----------------------------------------------------------------------------
class SourceGloLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SourceGloLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, tokens::panelAlt);
        setColour (juce::PopupMenu::textColourId, tokens::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, tokens::panelHigh);
        setColour (juce::PopupMenu::highlightedTextColourId, tokens::cyan);
        setColour (juce::TooltipWindow::backgroundColourId, tokens::panelAlt);
        setColour (juce::TooltipWindow::textColourId, tokens::text);
        setColour (juce::TooltipWindow::outlineColourId, tokens::stroke);
    }

    juce::Font getPopupMenuFont() override  { return Fonts::bodyValue(); }
};

} // namespace sourceglo
