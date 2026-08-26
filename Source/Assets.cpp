#include "Assets.h"

namespace sourceglo
{

// -----------------------------------------------------------------------------
//  Failure bookkeeping - one diagnostic per distinct missing asset, then quiet.
// -----------------------------------------------------------------------------
static juce::CriticalSection& failureLock()
{
    static juce::CriticalSection lock;
    return lock;
}

static juce::StringArray& reportedFailures()
{
    static juce::StringArray reported;
    return reported;
}

static void logMissOnce (const juce::String& relativePath)
{
    const juce::ScopedLock sl (failureLock());
    if (reportedFailures().contains (relativePath))
        return;
    reportedFailures().add (relativePath);
    juce::Logger::writeToLog ("SourceGlo Pro: asset failed to load, using flat fallback: "
                                + relativePath);
}

int Assets::loadFailureCount()
{
    const juce::ScopedLock sl (failureLock());
    return reportedFailures().size();
}

juce::String Assets::describeFailures()
{
    const juce::ScopedLock sl (failureLock());
    return reportedFailures().joinIntoString ("\n");
}

// -----------------------------------------------------------------------------
//  Locator - bundle Resources/Assets, Windows exe-relative Assets, or (for the
//  headless tools) an Assets/ folder up the tree from the executable.
// -----------------------------------------------------------------------------
juce::File Assets::assetsDirectory()
{
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    auto resources = exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/Assets");
    if (resources.getChildFile ("Base").isDirectory())
        return resources;

    auto beside = exe.getParentDirectory().getChildFile ("Assets");
    if (beside.getChildFile ("Base").isDirectory())
        return beside;

    auto dir = exe.getParentDirectory();
    for (int i = 0; i < 6; ++i)
    {
        auto candidate = dir.getChildFile ("Assets");
        if (candidate.getChildFile ("Base").isDirectory())
            return candidate;
        dir = dir.getParentDirectory();
    }
    return {};
}

juce::Image Assets::load (const juce::String& relativePath)
{
    auto file = assetsDirectory().getChildFile (relativePath);
    if (file.existsAsFile())
    {
        // One decode per file per process; every instance shares it.
        auto img = juce::ImageCache::getFromFile (file);
        if (img.isValid())
            return img;
    }
    logMissOnce (relativePath);
    return {};
}

// -----------------------------------------------------------------------------
//  Base + brand
// -----------------------------------------------------------------------------
juce::Image Assets::shell()
{
    static const juce::Image img = load ("Base/sourceglo_shell_1491x1055.png");
    return img;
}

juce::Image Assets::shell2x()
{
    static const juce::Image img = load ("Base/sourceglo_shell_2982x2110_2x.png");
    return img;
}

juce::Image Assets::logoHeader (float scale)
{
    // Exact-size exports; LOGO_USAGE_GUIDE.md says use the native asset for
    // higher scales rather than stretching the 1x image. The exports are
    // near-tight (312x38 opaque in the 320x42 canvas) so they draw 1:1.
    static const juce::Image x1 = load ("Brand/sourceglo_pro_premium_logo_header_320x42.png");
    static const juce::Image x2 = load ("Brand/sourceglo_pro_premium_logo_header_640x84.png");
    static const juce::Image x4 = load ("Brand/sourceglo_pro_premium_logo_header_1280x168.png");

    if (scale > 2.01f && x4.isValid()) return x4;
    if (scale > 1.01f && x2.isValid()) return x2;
    return x1;
}

juce::Image Assets::logoFull()
{
    static const juce::Image full = load ("Brand/sourceglo_pro_premium_logo_2048w.png");
    return full;
}

juce::Image Assets::premiumMark (int px)
{
    auto raw = load ("Brand/sourceglo_premium_mark_" + juce::String (px) + ".png");
    if (! raw.isValid())
        return raw;

    // The supplied mark files are a bad crop of the full lockup: a sliver of
    // the wordmark's "S" is baked into the right edge at every size (the
    // mark's own ink ends at 90.3% of the width, the sliver starts at 97%).
    // Trimming at 94% drops it with margin on both sides. Verified against
    // the 256/512/1024 files - this is the art, not a load failure, so it is
    // corrected here rather than by redrawing anything.
    return raw.getClippedImage (raw.getBounds().withWidth (
               juce::roundToInt (raw.getWidth() * 0.94f)));
}

// -----------------------------------------------------------------------------
//  HUD
// -----------------------------------------------------------------------------
// The ring export carries huge transparent margin: 445x445 of art centred in
// a 1024 canvas. Aspect-fitting the raw canvas would draw the ring at ~40 %
// size (the same trap as the MasterGlo / Drum King logos), so the opaque
// bounds are measured once at load and the image is cropped to them.
static juce::Image trimToOpaqueBounds (juce::Image full)
{
    if (! full.isValid())
        return full;

    juce::Image::BitmapData pixels (full, juce::Image::BitmapData::readOnly);
    int minX = full.getWidth(), minY = full.getHeight(), maxX = -1, maxY = -1;

    for (int y = 0; y < full.getHeight(); ++y)
        for (int x = 0; x < full.getWidth(); ++x)
            if (pixels.getPixelColour (x, y).getAlpha() > 8)
            {
                minX = juce::jmin (minX, x); maxX = juce::jmax (maxX, x);
                minY = juce::jmin (minY, y); maxY = juce::jmax (maxY, y);
            }

    if (maxX < minX || maxY < minY)
        return full;

    const juce::Rectangle<int> art (minX, minY, maxX - minX + 1, maxY - minY + 1);
    if (art == full.getBounds())
        return full;

    return full.getClippedImage (art).createCopy();
}

juce::Image Assets::scoreRingBase()
{
    static const juce::Image img = trimToOpaqueBounds (load ("HUD/source_score_ring_base_1024.png"));
    return img;
}

juce::Image Assets::metricPod (PodColour c)
{
    switch (c)
    {
        case PodColour::red:   return load ("HUD/metric_pod_red_256.png");
        case PodColour::green: return load ("HUD/metric_pod_green_256.png");
        case PodColour::gold:  return load ("HUD/metric_pod_gold_256.png");
        case PodColour::cyan:
        default:               return load ("HUD/metric_pod_cyan_256.png");
    }
}

juce::Image Assets::statusPill (const juce::String& phrase)
{
    // 2x exports (220x42 at 1x is drawn ~150x28, so 2x stays sharp on Retina).
    const auto key = phrase.toLowerCase().replaceCharacter (' ', '_');
    return load ("HUD/status_pill_" + key + "_2x.png");
}

// -----------------------------------------------------------------------------
//  Controls
// -----------------------------------------------------------------------------

// Slice a vertical filmstrip into per-frame images at load. Confirmed in
// production (MasterGlo Pro, 2026-08-19): drawing from the tall strip works in
// the standalone and is silently MISSING in a DAW once the strip exceeds the
// renderer's texture limit. createCopy(), not getClippedImage() alone - a
// clipped image is only a view onto the oversized original.

// The supplied strips are rotated +90 degrees from their own spec sheet:
// filmstrip_spec.md says frame 0 points -135 degrees (7:30), but the art's
// frame 0 points -45 (10:30) and frame 127 points +225 (7:30) - measured off
// the frames on 2026-08-24. One exact 90-degree anticlockwise rotation per
// frame restores the -135..+135 sweep AND puts the bezel specular back at the
// top where the approved mockup has it. Pixel transpose, no resampling.
static juce::Image rotateFrameAnticlockwise (const juce::Image& src)
{
    const int size = src.getWidth();
    juce::Image dst (juce::Image::ARGB, size, size, true);
    juce::Image::BitmapData in  (src, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData out (dst, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            out.setPixelColour (y, size - 1 - x, in.getPixelColour (x, y));

    return dst;
}

static Assets::Filmstrip sliceStrip (juce::Image strip, int frameSize)
{
    Assets::Filmstrip fs;
    if (! strip.isValid() || frameSize <= 0)
        return fs;

    // Trust the image, not the filename.
    const int size  = strip.getWidth();
    const int count = strip.getHeight() / juce::jmax (1, size);
    fs.frameSize = size;

    if (count >= 2)
    {
        fs.frames.reserve ((size_t) count);
        for (int f = 0; f < count; ++f)
            fs.frames.push_back (rotateFrameAnticlockwise (
                strip.getClippedImage ({ 0, f * size, size, size }).createCopy()));
    }
    return fs;
}

const Assets::Filmstrip& Assets::macroKnob()
{
    static const Filmstrip fs = []
    {
        auto result = sliceStrip (load ("Controls/knobs/macro_knob_96px_128frames.png"), 96);
        juce::ImageCache::releaseUnusedImages();   // drop the tall source image
        return result;
    }();
    return fs;
}

const Assets::Filmstrip& Assets::trimKnob()
{
    static const Filmstrip fs = []
    {
        auto result = sliceStrip (load ("Controls/knobs/trim_knob_52px_128frames.png"), 52);
        juce::ImageCache::releaseUnusedImages();
        return result;
    }();
    return fs;
}

juce::Image Assets::button (ButtonKind kind, ButtonState state)
{
    static const char* kinds[]  = { "main_cyan", "main_gold", "main_neutral",
                                    "small_cyan", "small_gold", "browse_library" };
    static const char* states[] = { "normal", "hover", "down", "disabled" };

    // 2x exports so scaled draws stay sharp.
    return load (juce::String ("Controls/buttons/") + kinds[(int) kind]
                   + "_" + states[(int) state] + "_2x.png");
}

juce::Image Assets::dropdown()
{
    static const juce::Image img = load ("Controls/buttons/dropdown_210x34_2x.png");
    return img;
}

juce::Image Assets::tabActive()
{
    static const juce::Image img = load ("Controls/buttons/tab_active_150x42_2x.png");
    return img;
}

juce::Image Assets::toggle (bool on)
{
    return load (juce::String ("Controls/toggles/toggle_") + (on ? "on" : "off")
                   + "_56x26_2x.png");
}

juce::Image Assets::meterTrough()
{
    static const juce::Image img = load ("Controls/meters/vertical_meter_trough_24x154.png");
    return img;
}

juce::Image Assets::meterSegment()
{
    static const juce::Image img = load ("Controls/meters/vertical_meter_segment_14x7.png");
    return img;
}

// -----------------------------------------------------------------------------
//  Cards
// -----------------------------------------------------------------------------
juce::Image Assets::diagnosticCard (int severity)
{
    static const char* names[] = { "high", "medium", "good" };
    return load (juce::String ("Cards/diagnostic_card_")
                   + names[juce::jlimit (0, 2, severity)] + "_380x84_2x.png");
}

juce::Image Assets::rescueRow (int state)
{
    static const char* names[] = { "normal", "hover", "selected" };
    return load (juce::String ("Cards/rescue_row_")
                   + names[juce::jlimit (0, 2, state)] + "_388x48_2x.png");
}

// -----------------------------------------------------------------------------
//  Analyzers
// -----------------------------------------------------------------------------
juce::Image Assets::spectrumGrid()
{
    static const juce::Image img = load ("Analyzers/spectrum_grid_790x460.png");
    return img;
}

juce::Image Assets::radarGrid()
{
    static const juce::Image img = load ("Analyzers/radar_grid_520.png");
    return img;
}

juce::Image Assets::conflictBand()
{
    static const juce::Image img = load ("Analyzers/conflict_band_red_240x420.png");
    return img;
}

// -----------------------------------------------------------------------------
//  Icons - supplied SVGs recoloured by rewriting colour attributes in the SVG
//  text before parsing, then cached per (name, tint). The supplied icons use
//  hardcoded stroke colours (#35E7FF, #AAB7BC, ...), so attribute rewriting is
//  the reliable way to tint them without touching the art itself.
// -----------------------------------------------------------------------------
juce::Drawable* Assets::icon (const juce::String& name, juce::Colour tint)
{
    struct Key
    {
        juce::String name; juce::uint32 argb;
        bool operator< (const Key& o) const
        {
            if (name != o.name) return name < o.name;
            return argb < o.argb;
        }
    };

    static juce::CriticalSection lock;
    static std::map<Key, std::unique_ptr<juce::Drawable>> cache;

    const Key key { name, tint.getARGB() };
    const juce::ScopedLock sl (lock);

    auto it = cache.find (key);
    if (it != cache.end())
        return it->second.get();

    std::unique_ptr<juce::Drawable> drawable;

    auto file = assetsDirectory().getChildFile ("Icons/" + name + ".svg");
    if (file.existsAsFile())
    {
        auto svg = file.loadFileAsString();
        const auto hex = tint.toDisplayString (false);
        svg = svg.replaceCharacters ("\r", " ");

        // stroke="#XXXXXX" / fill="#XXXXXX" -> requested tint (keeps "none").
        for (const char* attr : { "stroke=\"#", "fill=\"#" })
        {
            int pos = 0;
            while ((pos = svg.indexOf (pos, attr)) >= 0)
            {
                const int valueStart = pos + (int) juce::String (attr).length();
                int valueEnd = valueStart;
                while (valueEnd < svg.length() && svg[valueEnd] != '"')
                    ++valueEnd;
                svg = svg.substring (0, valueStart) + hex + svg.substring (valueEnd);
                pos = valueStart;
            }
        }

        drawable = juce::Drawable::createFromSVGString (svg);
    }

    if (drawable == nullptr)
        logMissOnce ("Icons/" + name + ".svg");

    auto* raw = drawable.get();
    cache[key] = std::move (drawable);
    return raw;
}

} // namespace sourceglo
