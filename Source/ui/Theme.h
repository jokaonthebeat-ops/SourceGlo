/*
    Theme.h - colour tokens, font resolution and the design-space layout.

    Every rectangle is in the approved 1491 x 1055 design coordinate system
    (Spec/.../08_LAYOUT/layout_1491x1055.json). The editor scales one content
    component uniformly, so nothing here ever needs runtime scaling maths.

    NOTE: juce::Rectangle has no constexpr constructor in JUCE 9, so these
    tables are `inline const`, not `constexpr` (same trap as MasterGlo Pro).
*/

#pragma once
#include <JuceHeader.h>

namespace sourceglo
{

// Set by the headless tools (make uishot) so display timers keep updating
// without a visible window peer; isShowing() is false headlessly.
inline bool& headlessRefreshMode()
{
    static bool mode = false;
    return mode;
}

// -----------------------------------------------------------------------------
//  Colour tokens - Spec/.../08_LAYOUT/color_tokens.json, verbatim.
// -----------------------------------------------------------------------------
namespace tokens
{
    inline const juce::Colour bg0        { 0xff03070a };
    inline const juce::Colour bg1        { 0xff061016 };
    inline const juce::Colour bg2        { 0xff0a141a };
    inline const juce::Colour panel      { 0xff0a1217 };
    inline const juce::Colour panelAlt   { 0xff0d171d };
    inline const juce::Colour panelHigh  { 0xff111d24 };
    inline const juce::Colour stroke     { 0xff26343b };
    inline const juce::Colour strokeSoft { 0xff17262d };
    inline const juce::Colour white      { 0xfff2f5f6 };
    inline const juce::Colour text       { 0xffd7dee1 };
    inline const juce::Colour muted      { 0xff8e9ba1 };
    inline const juce::Colour bodyLabel  { 0xffaeb8bc };   // typography.md "Body label"
    inline const juce::Colour hudLabel   { 0xffcdd6d9 };   // typography.md "HUD label"
    inline const juce::Colour metricLbl  { 0xffdde4e6 };   // typography.md "Metric label"
    inline const juce::Colour buttonLbl  { 0xffeaf0f2 };   // typography.md "Button label"
    inline const juce::Colour cyan       { 0xff35e7ff };
    inline const juce::Colour cyanMid    { 0xff16bfd8 };
    inline const juce::Colour cyanDark   { 0xff087687 };
    inline const juce::Colour gold       { 0xfff3b547 };
    inline const juce::Colour goldMid    { 0xffd98b20 };
    inline const juce::Colour red        { 0xffff5865 };
    inline const juce::Colour amber      { 0xfff0aa34 };
    inline const juce::Colour green      { 0xff56dd7b };
}

// -----------------------------------------------------------------------------
//  Fonts - system lookup only, no bundled files (typography.md).
//  Preferred order: Inter Display, Inter, SF Pro Display, Segoe UI,
//  Helvetica Neue, Arial. Resolved once per process.
// -----------------------------------------------------------------------------
struct Fonts
{
    static const juce::String& family()
    {
        static const juce::String resolved = []
        {
            const juce::StringArray installed = juce::Font::findAllTypefaceNames();
            for (const char* want : { "Inter Display", "Inter", "SF Pro Display",
                                      "Segoe UI", "Helvetica Neue", "Arial" })
                if (installed.contains (juce::String (want)))
                    return juce::String (want);
            return juce::Font (juce::FontOptions{}).getTypefaceName();
        }();
        return resolved;
    }

    // medium ~ weight 500, bold ~ 600-700. System lookup only: a family that
    // ships a real Medium style gets it, otherwise the nearest of plain/bold.
    static juce::Font make (float px, bool medium = false, bool bold = false)
    {
        if (bold)
            return juce::Font (juce::FontOptions (family(), px, juce::Font::bold));

        if (medium)
        {
            juce::Font f (juce::FontOptions (family(), "Medium", px));
            if (f.getTypefacePtr() != nullptr && f.getTypefaceStyle() == "Medium")
                return f;
        }
        return juce::Font (juce::FontOptions (family(), px, juce::Font::plain));
    }

    // Typography roles (typography.md). Sizes are design-space pixels.
    static juce::Font panelTitle()  { return make (17.0f, false, true).withExtraKerningFactor (0.035f); }
    static juce::Font fieldLabel()  { return make (12.0f, false, true).withExtraKerningFactor (0.04f); }
    static juce::Font bodyLabel()   { return make (13.0f); }
    static juce::Font bodyValue()   { return make (14.0f, true); }
    static juce::Font mainScore()   { return make (82.0f, false, true).withExtraKerningFactor (-0.02f); }
    static juce::Font hudLabel()    { return make (15.0f, true).withExtraKerningFactor (0.06f); }
    static juce::Font metricLabel() { return make (13.0f, false, true).withExtraKerningFactor (0.05f); }
    static juce::Font metricValue() { return make (26.0f, false, true); }
    static juce::Font tab()         { return make (15.0f, true).withExtraKerningFactor (0.05f); }
    static juce::Font buttonLabel() { return make (14.0f, false, true).withExtraKerningFactor (0.025f); }
    static juce::Font diagTitle()   { return make (15.0f, false, true); }
    static juce::Font diagBody()    { return make (12.0f); }
    static juce::Font rescueTitle() { return make (14.0f, true); }
    static juce::Font rescueTag()   { return make (11.0f); }
    static juce::Font footer()      { return make (12.0f, true); }
};

// -----------------------------------------------------------------------------
//  Layout - primary bounds from layout_1491x1055.json (the declared authority),
//  plus internal geometry measured from the approved mockup.
// -----------------------------------------------------------------------------
struct Design
{
    static constexpr int width  = 1491;
    static constexpr int height = 1055;
    static constexpr float aspect = 1491.0f / 1055.0f;
    static constexpr int minWidth  = 1044;   // responsive_rules.md, 70 % scale
    static constexpr int minHeight = 739;
    static constexpr int maxWidth  = 2237;   // 150 %
    static constexpr int maxHeight = 1583;
};

namespace layout
{
    using R = juce::Rectangle<int>;

    // --- primary bounds: layout_1491x1055.json, verbatim -----------------
    inline const R header          {    6,   4, 1479,  64 };
    inline const R logo            {   19,  16,  320,  42 };
    inline const R presetPrev      {  750,  18,   42,  40 };
    inline const R presetBox       {  790,  18,  222,  40 };
    inline const R presetNext     { 1010,  18,   42,  40 };
    inline const R headerUtilities { 1068,  17,  401,  42 };
    inline const R sourcePanel     {   10,  75,  243, 894 };
    inline const R heroPanel       {  254,  75,  780, 445 };
    inline const R diagnosticsPanel{ 1037,  75,  444, 446 };
    inline const R scoreRing       {  492,  81,  414, 392 };
    inline const R metricTone      {  399, 114,   96,  96 };
    inline const R metricPunch     {  392, 279,   96,  96 };
    inline const R metricLevel     {  882, 114,   96,  96 };
    inline const R metricPhase     {  886, 279,   96,  96 };
    inline const R metricFit       {  642, 376,  102, 102 };
    inline const R btnAnalyze      {  350, 462,  187,  43 };
    inline const R btnFixSource    {  574, 462,  199,  43 };
    inline const R btnAB           {  807, 462,  168,  43 };
    inline const R lowerMainPanel  {  253, 528,  760, 441 };
    inline const R tabs            {  253, 528,  760,  41 };
    inline const R spectrumPanel   {  262, 574,  395, 263 };
    inline const R fitPanel        {  665, 574,  339, 263 };
    inline const R macrosPanel     {  253, 842,  760, 127 };
    inline const R rescuePanel     { 1017, 528,  464, 441 };
    inline const R rescueHeader    { 1017, 528,  464,  42 };
    inline const R rescueList      { 1034, 580,  388, 274 };
    inline const R browseLibrary   { 1034, 890,  386,  45 };
    inline const R footer          {    6, 976, 1479,  72 };

    // --- internal geometry, measured from the approved mockup ------------
    // The macro strip in the mockup visually spans the full width beneath
    // both the source panel and the lower main panel (MACROS label at x~33,
    // knobs from ~110 to ~954), wider than macros_panel in the JSON - and its
    // value readouts sit at y~976..992, riding the footer well's top edge
    // exactly as the approved reference draws them.
    inline const R macroStrip      {   20, 848,  993, 148 };

    // Spectrum plot area inside spectrumPanel: 20 Hz..20 kHz maps to
    // x 299..661, +12..-60 dB maps to y 631..835 (measured off the mockup's
    // axis labels; ~120.5 px per decade, ~2.83 px per dB).
    inline const R spectrumPlot    {  299, 625,  362, 210 };

    inline const R diagCard1       { 1069, 141,  380,  84 };
    inline const int diagCardPitch = 98;
}

} // namespace sourceglo
