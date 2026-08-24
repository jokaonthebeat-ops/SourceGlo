// -----------------------------------------------------------------------------
//  SourceGlo Pro - deterministic test suite (UI milestone scope).
//
//  What this proves at this milestone:
//    * the parameter contract matches Spec/.../08_LAYOUT/parameters.json,
//      read back from the JSON at runtime - code drift fails the test
//    * gain / phase / mono / bypass audio behaviour, with absolute targets
//    * state save/load round-trip including the UI scale
//    * editor open/close is stable and every control has an accessible name
//    * all supplied artwork loads
//
//  Grows with the DSP milestones; the harness pattern is EQGlo Pro's.
// -----------------------------------------------------------------------------

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <cstdio>

using namespace sourceglo;

static int checks = 0, failures = 0;

static void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL: %s\n", what.toRawUTF8());
    }
}

static void checkNear (double value, double target, double tol, const juce::String& what)
{
    check (std::abs (value - target) <= tol,
           what + " (got " + juce::String (value, 4) + ", want "
                + juce::String (target, 4) + " +/- " + juce::String (tol, 4) + ")");
}

// --- feed a sine block and return output RMS ---------------------------------
static double processSineRms (SourceGloProcessor& p, double freq, float amp,
                              int blocks = 30, bool* phaseFlipped = nullptr)
{
    const double sr = 48000.0;
    const int blockSize = 512;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;
    double sumSq = 0.0;
    int count = 0;
    bool flipped = true;

    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> dry (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float v = amp * (float) std::sin (phase);
            phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
            dry.setSample (0, i, v);
            dry.setSample (1, i, v);
        }

        p.processBlock (buffer, midi);

        if (b >= blocks / 2)     // let smoothing settle
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float out = buffer.getSample (0, i);
                sumSq += (double) out * out;
                ++count;
                if (dry.getSample (0, i) * out > 1.0e-9f)
                    flipped = false;
            }
        }
    }

    if (phaseFlipped != nullptr)
        *phaseFlipped = flipped;
    return std::sqrt (sumSq / juce::jmax (1, count));
}

static void setParam (SourceGloProcessor& p, const char* id, float value)
{
    auto* param = p.getAPVTS().getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

// -----------------------------------------------------------------------------
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("SourceGlo Pro test suite\n========================\n");

    // ---------------------------------------------------------------- assets
    std::printf ("- artwork\n");
    {
        check (Assets::assetsDirectory().isDirectory(), "assets directory found");
        check (Assets::shell().isValid(), "shell loads");
        check (Assets::logoHeader (1.0f).isValid(), "header logo loads");
        check (Assets::logoHeader (1.0f).getWidth() == 320
                && Assets::logoHeader (1.0f).getHeight() == 42, "header logo is the 320x42 export");
        check (Assets::scoreRingBase().isValid(), "score ring base loads");
        check (Assets::macroKnob().isValid()
                && (int) Assets::macroKnob().frames.size() == 128, "macro filmstrip slices to 128 frames");
        check (Assets::macroKnob().frames[0].getWidth() == 96, "macro frame is 96 px");
        check (Assets::trimKnob().isValid()
                && (int) Assets::trimKnob().frames.size() == 128, "trim filmstrip slices to 128 frames");
        for (int k = 0; k < 6; ++k)
            for (int s = 0; s < 4; ++s)
                check (Assets::button ((ButtonKind) k, (ButtonState) s).isValid(),
                       "button art kind " + juce::String (k) + " state " + juce::String (s));
        for (int i = 0; i < 3; ++i)
            check (Assets::diagnosticCard (i).isValid(), "diagnostic card " + juce::String (i));
        for (int i = 0; i < 3; ++i)
            check (Assets::rescueRow (i).isValid(), "rescue row " + juce::String (i));
        check (Assets::spectrumGrid().isValid(), "spectrum grid loads");
        check (Assets::radarGrid().isValid(), "radar grid loads");
        for (const char* phrase : { "READY", "GOOD", "NEEDS WORK", "FIX REQUIRED" })
            check (Assets::statusPill (phrase).isValid(),
                   juce::String ("status pill for ") + phrase);
        check (Assets::icon ("analyze", juce::Colours::white) != nullptr, "icons parse");
    }

    // ---------------------------------------------- parameter contract vs JSON
    std::printf ("- parameter contract vs Spec parameters.json\n");
    {
        SourceGloProcessor p;

        juce::File specFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        for (int i = 0; i < 6 && ! specFile.getChildFile ("Spec").isDirectory(); ++i)
            specFile = specFile.getParentDirectory();
        auto json = juce::JSON::parse (specFile.getChildFile (
            "Spec/SourceGlo_Pro_UI_Assets_v1.1/08_LAYOUT/parameters.json").loadFileAsString());

        auto* list = json.getArray();
        check (list != nullptr, "parameters.json parses");

        if (list != nullptr)
        {
            check (list->size() == 17, "spec defines 17 parameters");
            for (const auto& entry : *list)
            {
                const auto id   = entry["id"].toString();
                const auto type = entry["type"].toString();
                auto* param = p.getAPVTS().getParameter (id);
                check (param != nullptr, "parameter exists: " + id);
                if (param == nullptr)
                    continue;

                if (type == "float")
                {
                    auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param);
                    checkNear (rp->getNormalisableRange().start, (double) entry["min"], 1e-6,
                               id + " min");
                    checkNear (rp->getNormalisableRange().end, (double) entry["max"], 1e-6,
                               id + " max");
                    checkNear ((double) rp->convertFrom0to1 (rp->getDefaultValue()),
                               (double) entry["default"], 1e-4, id + " default");
                }
                else if (type == "bool")
                {
                    const bool def = (bool) entry["default"];
                    checkNear (param->getDefaultValue(), def ? 1.0 : 0.0, 1e-6, id + " default");
                }
                else if (type == "choice")
                {
                    auto* cp = dynamic_cast<juce::AudioParameterChoice*> (param);
                    check (cp != nullptr, id + " is a choice");
                    if (cp != nullptr)
                    {
                        auto* choices = entry["choices"].getArray();
                        check (choices != nullptr
                                && cp->choices.size() == choices->size(),
                               id + " choice count");
                        if (choices != nullptr)
                            for (int c = 0; c < choices->size() && c < cp->choices.size(); ++c)
                                check (cp->choices[c] == (*choices)[c].toString(),
                                       id + " choice " + juce::String (c));
                        auto* base = static_cast<juce::RangedAudioParameter*> (cp);
                        const int defIndex = (int) (base->getDefaultValue()
                                * (float) (cp->choices.size() - 1) + 0.5f);
                        check (cp->choices[defIndex] == entry["default"].toString(),
                               id + " default choice");
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------ audio
    std::printf ("- audio path\n");
    {
        SourceGloProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        // Unity: -20 dBFS sine in -> same RMS out (sine RMS = amp/sqrt2).
        const double unityRms = processSineRms (p, 1000.0, 0.1f);
        checkNear (juce::Decibels::gainToDecibels (unityRms),
                   juce::Decibels::gainToDecibels (0.1 / juce::MathConstants<double>::sqrt2),
                   0.05, "unity gain RMS");

        // +6 dB input trim.
        setParam (p, pid::inputGain, 6.0f);
        const double plus6 = processSineRms (p, 1000.0, 0.1f);
        checkNear (juce::Decibels::gainToDecibels (plus6 / unityRms), 6.0, 0.1,
                   "+6 dB input gain");
        setParam (p, pid::inputGain, 0.0f);

        // -6 dB output trim.
        setParam (p, pid::outputGain, -6.0f);
        const double minus6 = processSineRms (p, 1000.0, 0.1f);
        checkNear (juce::Decibels::gainToDecibels (minus6 / unityRms), -6.0, 0.1,
                   "-6 dB output gain");
        setParam (p, pid::outputGain, 0.0f);

        // Phase invert flips polarity without changing level.
        setParam (p, pid::phaseInvert, 1.0f);
        bool flipped = false;
        const double invRms = processSineRms (p, 1000.0, 0.1f, 30, &flipped);
        check (flipped, "phase invert flips polarity");
        checkNear (invRms / unityRms, 1.0, 0.02, "phase invert keeps level");
        setParam (p, pid::phaseInvert, 0.0f);

        // Mono: L-only input becomes equal L/R halves.
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            setParam (p, pid::mono, 1.0f);
            for (int b = 0; b < 12; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    buffer.setSample (0, i, 0.5f);
                    buffer.setSample (1, i, 0.0f);
                }
                p.processBlock (buffer, midi);
            }
            checkNear (buffer.getSample (0, 500), 0.25, 0.01, "mono sums L");
            checkNear (buffer.getSample (1, 500), 0.25, 0.01, "mono sums R");
            setParam (p, pid::mono, 0.0f);
        }

        // Bypass passes input through untouched even with gain cranked.
        setParam (p, pid::inputGain, 12.0f);
        setParam (p, pid::bypass, 1.0f);
        const double bypassRms = processSineRms (p, 1000.0, 0.1f);
        checkNear (bypassRms / unityRms, 1.0, 0.02, "bypass is unity");
        setParam (p, pid::bypass, 0.0f);
        setParam (p, pid::inputGain, 0.0f);

        // Meters saw signal.
        check (p.inPeak[0].load() > 0.01f, "input meter tap alive");
        check (p.outPeak[0].load() > 0.01f, "output meter tap alive");
    }

    // ------------------------------------------------------------------ state
    std::printf ("- state round-trip\n");
    {
        SourceGloProcessor a;
        setParam (a, pid::punch, 83.0f);
        setParam (a, pid::sourceType, 4.0f);
        a.setSavedUIScale (1.25f);
        a.selectPreset (2);

        juce::MemoryBlock blob;
        a.getStateInformation (blob);

        SourceGloProcessor b;
        b.setStateInformation (blob.getData(), (int) blob.getSize());

        checkNear ((double) b.getAPVTS().getRawParameterValue (pid::punch)->load(), 83.0, 0.01,
                   "punch survives round-trip");
        checkNear ((double) b.getAPVTS().getRawParameterValue (pid::sourceType)->load(), 4.0, 0.01,
                   "source type survives round-trip");
        checkNear ((double) b.getSavedUIScale(), 1.25, 0.001, "UI scale survives round-trip");
        check (b.getPresetIndex() == 2, "preset index survives round-trip");
    }

    // ----------------------------------------------------------------- editor
    std::printf ("- editor\n");
    {
        SourceGloProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        for (int i = 0; i < 25; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            check (ed != nullptr, "editor creates");
            ed->setSize (Design::width, Design::height);
        }

        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (Design::width, Design::height);

        // Aspect lock: constrainer enforces 1491:1055.
        check (ed->getConstrainer() != nullptr
                && std::abs (ed->getConstrainer()->getFixedAspectRatio()
                             - (double) Design::aspect) < 1e-3,
               "aspect ratio locked");

        // Accessibility: every interactive component carries a title/tooltip.
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            if (dynamic_cast<juce::Button*> (&c) != nullptr
                 || dynamic_cast<juce::Slider*> (&c) != nullptr)
                check (c.getTitle().isNotEmpty() || c.getName().isNotEmpty(),
                       "control has accessible name: " + c.getTitle() + c.getName());
            for (auto* child : c.getChildren())
                walk (*child);
        };
        walk (*ed);
    }

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
