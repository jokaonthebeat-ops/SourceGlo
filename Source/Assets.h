/*
    Assets.h - loads and caches every piece of supplied artwork.

    The pack's art is the product's art (see the LOGO_USAGE_GUIDE and the
    standing rule: never substitute generated stand-ins). When a file fails to
    load the UI draws an obvious flat fallback and the failure is recorded so
    `make uishot` can report it - a wrong-looking UI is a *load* problem first.
*/

#pragma once
#include <JuceHeader.h>

namespace sourceglo
{

enum class ButtonKind { mainCyan, mainGold, mainNeutral, smallCyan, smallGold, browseLibrary };
enum class ButtonState { normal, hover, down, disabled };
enum class PodColour { cyan, red, green, gold };

struct Assets
{
    // --- diagnostics ------------------------------------------------------
    static int loadFailureCount();
    static juce::String describeFailures();
    static juce::File assetsDirectory();

    // --- base -------------------------------------------------------------
    static juce::Image shell();          // 1491 x 1055
    static juce::Image shell2x();        // 2982 x 2110, for scales > 1

    // --- brand ------------------------------------------------------------
    // Premium header lockup, exact 1x/2x/4x exports. Draw at {19,16,320,42};
    // pick the export closest above the current physical scale.
    static juce::Image logoHeader (float scale);
    static juce::Image premiumMark (int px); // 256 / 512 / 1024
    static juce::Image logoFull();           // the full wordmark, for video/print

    // --- HUD --------------------------------------------------------------
    static juce::Image scoreRingBase();  // 1024 sq
    static juce::Image metricPod (PodColour c);
    static juce::Image statusPill (const juce::String& phrase); // by status text

    // --- controls ---------------------------------------------------------
    struct Filmstrip
    {
        std::vector<juce::Image> frames;  // sliced at load; never draw the strip
        int frameSize = 0;
        bool isValid() const { return ! frames.empty(); }
        const juce::Image& frameFor (float norm01) const
        {
            const int i = juce::jlimit (0, (int) frames.size() - 1,
                                        juce::roundToInt (norm01 * (float) (frames.size() - 1)));
            return frames[(size_t) i];
        }
    };

    static const Filmstrip& macroKnob(); // 96 px frames
    static const Filmstrip& trimKnob();  // 52 px frames

    static juce::Image button (ButtonKind kind, ButtonState state);
    static juce::Image dropdown();       // 210 x 34
    static juce::Image tabActive();      // 150 x 42
    static juce::Image toggle (bool on); // 56 x 26
    static juce::Image meterTrough();    // 24 x 154
    static juce::Image meterSegment();   // 14 x 7

    // --- cards ------------------------------------------------------------
    static juce::Image diagnosticCard (int severity); // 0 high, 1 medium, 2 good
    static juce::Image rescueRow (int state);         // 0 normal, 1 hover, 2 selected

    // --- analyzers --------------------------------------------------------
    static juce::Image spectrumGrid();   // 790 x 460
    static juce::Image radarGrid();      // 520 sq
    static juce::Image conflictBand();   // 240 x 420

    // --- icons ------------------------------------------------------------
    // Supplied SVGs, recoloured by rewriting the stroke/fill attributes before
    // parsing. Cached per (name, colour). Never parsed during paint.
    static juce::Drawable* icon (const juce::String& name, juce::Colour tint);

private:
    static juce::Image load (const juce::String& relativePath);
};

} // namespace sourceglo
