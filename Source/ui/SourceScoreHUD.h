/*
    SourceScoreHUD - the hero: score ring, status, five metric pods and the
    Analyze / Fix Source / A/B actions. Bounds: layout::heroPanel
    {254,75,780,445}; coordinates are hero-local.

    HUD rendering per hud_rendering_notes.md: ring base image at the
    score_ring bounds, live gold arc -135..+135 degrees above it, decorative
    cyan arcs stay in the base art, all text and pod values drawn in code.
*/

#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

// The gold button doubles as the Fix Amount control: once the fix is
// engaged, dragging vertically on it rides the amount like a knob (with a
// live "FIX - N%" label); a plain click still toggles. The drag swallows the
// click via consumeDragFlag() so releasing a drag never disengages the fix.
class FixSourceButton : public AssetButton
{
public:
    explicit FixSourceButton (SourceGloProcessor& p)
        : AssetButton ("Fix Source", ButtonKind::mainGold, "FIX SOURCE", "fix_source"),
          processor (p) {}

    bool consumeDragFlag()
    {
        const bool was = dragConsumed;
        dragConsumed = false;
        return was;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        startValue = processor.getAPVTS().getRawParameterValue (pid::fixAmount)->load();
        AssetButton::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int dy = -e.getDistanceFromDragStartY();
        if (processor.isFixEngaged() && (dragging || std::abs (dy) >= 6))
        {
            auto* param = processor.getAPVTS().getParameter (pid::fixAmount);
            if (param == nullptr)
                return;
            if (! dragging)
            {
                dragging = true;
                param->beginChangeGesture();
            }
            param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                (startValue + (float) dy * 0.5f) * 0.01f));
            return;
        }
        AssetButton::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging)
        {
            dragging = false;
            dragConsumed = true;
            if (auto* param = processor.getAPVTS().getParameter (pid::fixAmount))
                param->endChangeGesture();
        }
        AssetButton::mouseUp (e);
    }

private:
    SourceGloProcessor& processor;
    bool dragging = false, dragConsumed = false;
    float startValue = 50.0f;
};

class SourceScoreHUD : public juce::Component, private juce::Timer,
                       private juce::ChangeListener
{
public:
    explicit SourceScoreHUD (SourceGloProcessor& p) : processor (p)
    {
        setTitle ("Source score");

        analyzeButton.setTooltip ("Analyze the current source");
        fixButton.setTooltip ("Apply intelligent source correction (analyze first)");
        abButton.setTooltip ("Compare against the unprocessed source");
        fixButton.setEnabled (false);   // until something has been analysed

        abButton.setClickingTogglesState (true);
        analyzeButton.setLabelColour (tokens::buttonLbl);
        fixButton.setIconTint (tokens::gold);
        abButton.setIconTint (tokens::text);

        addAndMakeVisible (analyzeButton);
        addAndMakeVisible (fixButton);
        addAndMakeVisible (abButton);

        analyzeButton.onClick = [this] { processor.requestAnalyze(); };
        fixButton.onClick     = [this]
        {
            if (! fixButton.consumeDragFlag())   // a drag adjusts, never toggles
                processor.requestFixSource();
        };
        abButton.onClick      = [this] { processor.setCompareRaw (abButton.getToggleState()); };

        processor.analysisChanged.addChangeListener (this);
        startTimerHz (30);
    }

    ~SourceScoreHUD() override
    {
        processor.analysisChanged.removeChangeListener (this);
    }

    void resized() override
    {
        // Mockup-measured: the reference draws the action row ~15-25 px right
        // of the layout JSON; the reference is the visual authority.
        analyzeButton.setBounds (109, 390, 187, 43);
        fixButton.setBounds     (339, 390, 199, 43);
        abButton.setBounds      (575, 390, 172, 43);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& model = processor.getAnalysis();

        // --- ring base: layout::scoreRing {492,81,414,392} -> local {238,6}.
        const juce::Rectangle<float> ringBox (247.0f, 16.0f, 404.0f, 382.0f);
        const float ringSize = juce::jmin (ringBox.getWidth(), ringBox.getHeight());
        const juce::Rectangle<float> ringRect (ringBox.getCentreX() - ringSize * 0.5f,
                                               ringBox.getCentreY() - ringSize * 0.5f,
                                               ringSize, ringSize);

        auto ring = Assets::scoreRingBase();
        if (ring.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (ring, ringRect, juce::RectanglePlacement::centred);
        }
        else
        {
            g.setColour (tokens::stroke);
            g.drawEllipse (ringRect.reduced (6.0f), 2.0f);
        }

        // --- live gold score arc. The track travels 270 degrees between -135
        // and +135; the approved mockup fills the gold from the +135 end
        // backwards (top -> clockwise -> bottom right), leaving the base
        // art's static cyan visible on the unfilled side.
        if (model.analyzed)
        {
            const float radius = ringSize * 0.5f * 0.765f;
            const float endRad   = juce::degreesToRadians (135.0f);
            const float startRad = endRad - juce::degreesToRadians (270.0f * displayScore / 100.0f);

            juce::Path arc;
            arc.addCentredArc (ringRect.getCentreX(), ringRect.getCentreY(),
                               radius, radius, 0.0f, startRad, endRad, true);
            g.setColour (tokens::gold.withAlpha (0.25f));
            g.strokePath (arc, juce::PathStrokeType (15.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
            g.setColour (tokens::gold);
            g.strokePath (arc, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // --- centre text stack.
        const int cx = (int) ringRect.getCentreX();
        const int score = juce::roundToInt (displayScore);

        const juce::String dash (juce::CharPointer_UTF8 ("\xe2\x80\x93\xe2\x80\x93"));

        g.setColour (model.analyzed ? tokens::white : tokens::muted);
        g.setFont (Fonts::mainScore());
        g.drawText (model.analyzed ? juce::String (score) : dash,
                    cx - 120, 95, 240, 84, juce::Justification::centred);

        g.setFont (Fonts::hudLabel());
        g.setColour (tokens::hudLabel);
        g.drawText ("SOURCE SCORE", cx - 120, 177, 240, 18, juce::Justification::centred);

        // The pill art is background-only; the phrase is drawn live on top.
        // Before the first analysis the pill invites one instead of judging.
        const auto phrase = model.analyzed ? ScoreStatus::phrase (score)
                                           : juce::String ("PRESS ANALYZE");
        const auto phraseColour = model.analyzed ? ScoreStatus::colour (score) : tokens::muted;
        auto pill = model.analyzed ? Assets::statusPill (phrase) : juce::Image();
        const juce::Rectangle<float> pillRect ((float) cx - 75.0f, 205.0f, 150.0f, 28.0f);
        if (pill.isValid())
            g.drawImage (pill, pillRect, juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (phraseColour.withAlpha (0.15f));
            g.fillRoundedRectangle (pillRect, 6.0f);
            g.setColour (phraseColour);
            g.drawRoundedRectangle (pillRect, 6.0f, 1.2f);
        }
        g.setColour (phraseColour);
        g.setFont (Fonts::make (13.5f, false, true).withExtraKerningFactor (0.09f));
        g.drawText (phrase, pillRect.toNearestInt(), juce::Justification::centred);

        g.setFont (Fonts::bodyLabel());
        g.setColour (tokens::muted);
        g.drawText ("Compared to",             cx - 120, 241, 240, 15, juce::Justification::centred);
        g.drawText ("modern pro standard",     cx - 120, 258, 240, 15, juce::Justification::centred);

        // --- pods: label / value / "/100". Layout rects hero-local.
        drawPod (g, { 153,  45, 96,  96 }, "TONE",  model.tone,  model.analyzed);
        drawPod (g, { 146, 210, 96,  96 }, "PUNCH", model.punch, model.analyzed);
        drawPod (g, { 636,  45, 96,  96 }, "LEVEL", model.level, model.analyzed);
        drawPod (g, { 640, 210, 96,  96 }, "PHASE", model.phase, model.analyzed);
        drawPod (g, { 388, 301, 102, 102 }, "FIT",  model.fit,   model.analyzed);
    }

private:
    static PodColour podColourFor (int value)
    {
        if (value >= 85) return PodColour::green;
        if (value >= 50) return PodColour::cyan;
        if (value >= 40) return PodColour::gold;
        return PodColour::red;
    }

    static juce::Colour podTextColour (int value)
    {
        if (value >= 85) return tokens::green;
        if (value >= 50) return tokens::cyan;
        if (value >= 40) return tokens::gold;
        return tokens::red;
    }

    void drawPod (juce::Graphics& g, juce::Rectangle<int> r,
                  const juce::String& label, int value, bool analyzed)
    {
        auto art = Assets::metricPod (analyzed ? podColourFor (value) : PodColour::cyan);
        if (art.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (art, r.toFloat(), juce::RectanglePlacement::centred);
        }
        else
        {
            g.setColour (tokens::panelHigh);
            g.fillEllipse (r.toFloat());
            g.setColour (tokens::stroke);
            g.drawEllipse (r.toFloat().reduced (1.0f), 1.5f);
        }

        const int h = r.getHeight();
        g.setFont (Fonts::metricLabel());
        g.setColour (tokens::metricLbl);
        g.drawText (label, r.getX(), r.getY() + (int) (h * 0.20f), r.getWidth(), 14,
                    juce::Justification::centred);

        g.setFont (Fonts::metricValue());
        g.setColour (analyzed ? podTextColour (value) : tokens::muted);
        g.drawText (analyzed ? juce::String (value)
                             : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93\xe2\x80\x93")),
                    r.getX(), r.getY() + (int) (h * 0.36f),
                    r.getWidth(), 28, juce::Justification::centred);

        g.setFont (Fonts::make (10.0f));
        g.setColour (tokens::muted);
        g.drawText ("/100", r.getX(), r.getY() + (int) (h * 0.66f), r.getWidth(), 12,
                    juce::Justification::centred);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const auto& model = processor.getAnalysis();
        fixButton.setEnabled (model.analyzed);
        fixButton.setToggleState (processor.isFixEngaged(), juce::dontSendNotification);
        fixButton.setTooltip (processor.isFixEngaged()
            ? "Fix engaged - drag up/down for the amount, click to release"
            : "Apply intelligent source correction (analyze first)");
        // A/B follows the processor (engaging the fix snaps back to A).
        abButton.setToggleState (processor.isComparingRaw(), juce::dontSendNotification);
        repaint();
    }

    void timerCallback() override
    {
        if (! isShowing() && ! headlessRefreshMode())
            return;

        // The gold button reads out the live fix amount while engaged.
        const auto wanted = processor.isFixEngaged()
            ? "FIX " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 "))
                + juce::String ((int) std::lround (
                      processor.getAPVTS().getRawParameterValue (pid::fixAmount)->load()))
                + "%"
            : juce::String ("FIX SOURCE");
        if (wanted != lastFixLabel)
        {
            lastFixLabel = wanted;
            fixButton.setLabel (wanted);
        }

        // Smooth the score toward its target - no abrupt jumps (master prompt).
        const float target = processor.getAnalysis().analyzed
                               ? (float) processor.getAnalysis().score : 0.0f;
        if (std::abs (displayScore - target) > 0.05f)
        {
            displayScore += (target - displayScore) * 0.12f;
            repaint (247, 16, 404, 382);
        }
    }

    SourceGloProcessor& processor;

    AssetButton analyzeButton { "Analyze",    ButtonKind::mainCyan,    "ANALYZE",    "analyze" };
    FixSourceButton fixButton { processor };
    AssetButton abButton      { "A / B",      ButtonKind::mainNeutral, "A / B",      "ab" };
    juce::String lastFixLabel;

    float displayScore = 0.0f;   // animates up on first open
};

} // namespace sourceglo
