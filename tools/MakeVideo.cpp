// -----------------------------------------------------------------------------
//  Renders the SourceGlo Pro demo film.
//
//    make video                          -> marketing/SourceGloPro-demo.mp4
//    make video ARGS="path/to/loop.wav"     drives the analysis with real audio
//                                           and uses the plugin's processed
//                                           output as the soundtrack
//
//  Every frame is the real editor rendering real analysis. The parameter moves,
//  the Analyze presses and the Fix Source engagement are a scripted timeline
//  applied to the actual plugin, so the scores, diagnostics, spectrum and
//  rescue rows on screen are measurements of the signal being processed - not
//  an animation of what they would look like.
//
//  Encoding is AVAssetWriter + VideoToolbox: this machine has no ffmpeg and no
//  Homebrew to install one, but AVFoundation is in the SDK and writes a
//  standard H.264 mp4. (Ported from MasterGlo Pro's proven pipeline.)
//
//  With no audio file the render is SILENT on purpose - a fabricated song under
//  a product video is worse than none. The meters and the analysis still work,
//  because a generated drum bed is pushed through the chain.
// -----------------------------------------------------------------------------

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Assets.h"
#include "../Source/ui/Theme.h"

#include <juce_audio_formats/juce_audio_formats.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include <array>
#include <cstdio>

using namespace sourceglo;

static constexpr int fps = 30;

// Set at startup: 1920x1080 for the landscape film, 1080x1920 for the reel.
static int videoWidth = 1920;
static int videoHeight = 1080;
static bool reelMode = false;

// The editor renders at its native design canvas and is downscaled into the
// frame - crisper than rendering small, and the aspect never has to be guessed.
static constexpr int panelWidth = 1491;
static constexpr int panelHeight = 1055;

/*
    Reel layout. A 1.41:1 panel fitted to 1080 wide is only 764 of 1920 pixels
    tall, so a single centred panel would leave most of the frame empty.
    The panel stays on screen the whole time at the top, and the act's own
    region is blown up underneath - every pixel carries real interface.
*/
namespace reel
{
    inline constexpr float logoY = 96.0f,   logoH = 120.0f;
    inline constexpr float titleY = 236.0f, titleH = 68.0f;
    inline constexpr float panelY = 322.0f, panelW = 1044.0f, panelH = 739.0f;
    inline constexpr float captionY = 1078.0f, captionH = 62.0f;
    inline constexpr float detailY = 1156.0f,  detailH = 764.0f;
}

/*
    Draws a region of the rendered panel into a destination rectangle, scaled
    to COVER it - the crop overflows and is clipped rather than leaving bars.
    `focus` is in the editor's own 1491x1055 canvas units, so callers can name
    regions the way Layout.h does.
*/
static void drawFocus (juce::Graphics& g, const juce::Image& panel,
                       juce::Rectangle<float> focus, juce::Rectangle<float> dest,
                       float alpha)
{
    if (! panel.isValid() || alpha <= 0.01f || focus.isEmpty())
        return;

    const float toRender = (float) panel.getWidth() / 1491.0f;
    auto src = (focus * toRender).getIntersection (panel.getBounds().toFloat());
    if (src.isEmpty())
        return;

    const float scale = juce::jmax (dest.getWidth() / src.getWidth(),
                                    dest.getHeight() / src.getHeight());

    // Draw the WHOLE panel scaled up, positioned so the focus centre lands on
    // the destination centre, and let the clip crop. Drawing the image into a
    // rect the size of the scaled source instead just squeezes the entire
    // panel into the band, which looks like a duplicate of the shot above.
    auto whole = juce::Rectangle<float> ((float) panel.getWidth() * scale,
                                         (float) panel.getHeight() * scale)
                   .withPosition (dest.getCentreX() - src.getCentreX() * scale,
                                  dest.getCentreY() - src.getCentreY() * scale);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (dest.toNearestInt());
    g.setOpacity (alpha);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (panel, whole, juce::RectanglePlacement::stretchToFit, false);
    g.setOpacity (1.0f);

    g.setColour (tokens::cyan.withAlpha (0.22f * alpha));
    g.drawRect (dest, 1.0f);
}

// --- helpers -----------------------------------------------------------------

struct Segment
{
    double start, end;
    juce::String title;
    juce::String caption;
    std::function<void (SourceGloProcessor&, SourceGloEditor&, double progress)> action;
    // Reel only: the part of the panel the detail band zooms into, in editor
    // canvas units. Empty means no detail band for this act.
    juce::Rectangle<float> focus {};
};

static float smoothstep (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return t * t * (3.0f - 2.0f * t);
}

static float envelopeFor (double t, double start, double end, double in, double out)
{
    if (t < start || t > end)
        return 0.0f;
    const float rising = (float) juce::jlimit (0.0, 1.0, (t - start) / juce::jmax (1.0e-6, in));
    const float falling = (float) juce::jlimit (0.0, 1.0, (end - t) / juce::jmax (1.0e-6, out));
    return smoothstep (juce::jmin (rising, falling));
}

static void setParam (SourceGloProcessor& p, const char* id, float realValue)
{
    if (auto* param = p.getAPVTS().getParameter (id))
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
            dynamic_cast<juce::RangedAudioParameter*> (param)->convertTo0to1 (realValue)));
}

static void loadPreset (SourceGloProcessor& p, const juce::String& name)
{
    auto& bank = p.getPresets();
    for (int i = 0; i < bank.getNumPresets(); ++i)
        if (bank.getPreset (i).name == name)
        {
            bank.load (i, false);
            return;
        }
}

// Runs once when a segment first becomes current, however many frames it spans.
static bool firstFrameOf (const Segment* seg, double progress)
{
    juce::ignoreUnused (seg);
    return progress < (1.0 / fps) / 1.0e-9 && progress <= 0.0001;
}

static void drawTracked (juce::Graphics& g, const juce::String& text,
                         juce::Rectangle<float> area, juce::Font font, float kerning)
{
    g.setFont (font.withExtraKerningFactor (kerning / juce::jmax (1.0f, font.getHeight())));
    g.drawText (text, area, juce::Justification::centred, false);
}

// --- audio -------------------------------------------------------------------

/*
    A kick-forward drum bed so the analysis has something this product is
    actually about: a kick on every beat, an 808 under it, hats between.
    Deliberately not presented as a soundtrack.
*/
class DemoSignal
{
public:
    explicit DemoSignal (double sampleRate) : sr (sampleRate) {}

    void render (juce::AudioBuffer<float>& buffer)
    {
        const int n = buffer.getNumSamples();
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);

        const double beat = 60.0 / 92.0;
        const double eighth = beat * 0.5;

        for (int i = 0; i < n; ++i)
        {
            const double t = (double) samplesDone / sr;
            const double intoBeat = std::fmod (t, beat);
            const double intoEighth = std::fmod (t, eighth);

            const double kickEnv = std::exp (-intoBeat * 22.0);
            kickPhase += 2.0 * juce::MathConstants<double>::pi
                           * (54.0 + 80.0 * kickEnv) / sr;
            const float kick = (float) (std::sin (kickPhase) * kickEnv * 0.80);

            const int bar = (int) (t / (beat * 2.0));
            const double subHz = (bar % 2 == 0) ? 51.9 : 69.3;
            subPhase += 2.0 * juce::MathConstants<double>::pi * subHz / sr;
            const float sub = (float) (std::sin (subPhase) * 0.26);

            const double hatEnv = std::exp (-intoEighth * 85.0);
            const float noise = random.nextFloat() * 2.0f - 1.0f;
            hatState += 0.55f * (noise - hatState);
            const float hat = (noise - hatState) * (float) hatEnv * 0.20f;

            airL += 0.02f * ((random.nextFloat() * 2.0f - 1.0f) - airL);
            airR += 0.02f * ((random.nextFloat() * 2.0f - 1.0f) - airR);

            l[i] = juce::jlimit (-1.0f, 1.0f, kick + sub + hat + airL * 1.1f);
            r[i] = juce::jlimit (-1.0f, 1.0f, kick + sub + hat * 0.9f + airR * 1.1f);
            ++samplesDone;
        }
    }

private:
    double sr, kickPhase = 0.0, subPhase = 0.0;
    juce::int64 samplesDone = 0;
    juce::Random random { 0x5061ce };
    float hatState = 0.0f, airL = 0.0f, airR = 0.0f;
};

// --- overlay drawing ---------------------------------------------------------

static void drawBackdrop (juce::Graphics& g)
{
    juce::Rectangle<float> full (0.0f, 0.0f, (float) videoWidth, (float) videoHeight);

    juce::ColourGradient bg (juce::Colour (0xff07131a), full.getCentreX(), full.getCentreY(),
                             juce::Colour (0xff02060a), full.getX(), full.getBottom(), true);
    g.setGradientFill (bg);
    g.fillRect (full);

    // The product's own cyan/gold bloom, low in the frame.
    juce::ColourGradient bloomL (tokens::cyan.withAlpha (0.13f), 340.0f, 1080.0f,
                                 juce::Colours::transparentBlack, 340.0f, 360.0f, true);
    g.setGradientFill (bloomL);
    g.fillRect (full);

    juce::ColourGradient bloomR (tokens::gold.withAlpha (0.10f), 1580.0f, 1080.0f,
                                 juce::Colours::transparentBlack, 1580.0f, 400.0f, true);
    g.setGradientFill (bloomR);
    g.fillRect (full);
}

static void drawCaption (juce::Graphics& g, const juce::String& text, float alpha,
                         juce::Rectangle<float> band, float fontHeight = 27.0f)
{
    if (text.isEmpty() || alpha <= 0.01f)
        return;

    const float ruleWidth = band.getWidth() * 0.40f * alpha;
    g.setColour (tokens::cyan.withAlpha (0.32f * alpha));
    g.fillRect (band.getCentreX() - ruleWidth * 0.5f, band.getY() - 13.0f, ruleWidth, 1.0f);

    g.setColour (tokens::text.withAlpha (0.94f * alpha));
    drawTracked (g, text, band, Fonts::make (fontHeight), fontHeight * 0.08f);
}

static void drawTitle (juce::Graphics& g, const juce::String& text, float alpha,
                       juce::Rectangle<float> area, float fontHeight, bool withScrim)
{
    if (text.isEmpty() || alpha <= 0.01f)
        return;

    if (withScrim)
    {
        g.setColour (juce::Colours::black.withAlpha (0.66f * alpha));
        g.fillRect (0.0f, area.getY() - 42.0f, (float) videoWidth, area.getHeight() + 84.0f);
    }

    g.setColour (tokens::white.withAlpha (alpha));
    drawTracked (g, text.toUpperCase(), area,
                 Fonts::make (fontHeight, false, true), fontHeight * 0.10f);
}

static void drawLogo (juce::Graphics& g, float alpha, float scale, float centreY)
{
    auto logo = Assets::logoFull();
    if (! logo.isValid() || alpha <= 0.01f)
        return;

    const float aspect = (float) logo.getWidth() / (float) logo.getHeight();
    const float w = (float) videoWidth * (reelMode ? 0.84f : 0.52f) * scale;
    const float h = w / aspect;

    auto target = juce::Rectangle<float> (w, h)
                    .withCentre ({ (float) videoWidth * 0.5f, centreY });

    g.setOpacity (alpha);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (logo, target, juce::RectanglePlacement::centred, false);
    g.setOpacity (1.0f);
}

static void drawMark (juce::Graphics& g, float alpha, float size, juce::Point<float> centre)
{
    auto mark = Assets::premiumMark (1024);
    if (! mark.isValid() || alpha <= 0.01f)
        return;

    g.setOpacity (alpha);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (mark, juce::Rectangle<float> (size, size).withCentre (centre),
                 juce::RectanglePlacement::centred, false);
    g.setOpacity (1.0f);
}

// --- juce::Image -> CVPixelBuffer --------------------------------------------

static bool appendFrame (AVAssetWriterInputPixelBufferAdaptor* adaptor,
                         AVAssetWriterInput* input,
                         const juce::Image& image, CMTime time)
{
    while (! input.readyForMoreMediaData)
        [NSThread sleepForTimeInterval: 0.002];

    CVPixelBufferRef pixelBuffer = nullptr;
    if (CVPixelBufferPoolCreatePixelBuffer (kCFAllocatorDefault, adaptor.pixelBufferPool,
                                            &pixelBuffer) != kCVReturnSuccess)
        return false;

    CVPixelBufferLockBaseAddress (pixelBuffer, 0);
    auto* dest = (juce::uint8*) CVPixelBufferGetBaseAddress (pixelBuffer);
    const size_t destStride = CVPixelBufferGetBytesPerRow (pixelBuffer);

    {
        // juce::Image ARGB is BGRA in memory on little-endian - the same layout
        // as kCVPixelFormatType_32BGRA, so this is a row copy, no conversion.
        juce::Image::BitmapData src (image, juce::Image::BitmapData::readOnly);
        const size_t rowBytes = (size_t) image.getWidth() * 4;
        for (int y = 0; y < image.getHeight(); ++y)
            std::memcpy (dest + (size_t) y * destStride, src.getLinePointer (y), rowBytes);
    }

    CVPixelBufferUnlockBaseAddress (pixelBuffer, 0);

    const bool ok = [adaptor appendPixelBuffer: pixelBuffer withPresentationTime: time];
    CVPixelBufferRelease (pixelBuffer);
    return ok;
}

// --- demo library ------------------------------------------------------------

/*
    The rescue rows need real files to rank. A headless render has no user
    library, so a small generated pack goes to a scratch folder - and the index
    override keeps it away from the customer's real index.
*/
static juce::File buildDemoLibrary()
{
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getChildFile ("SourceGloDemoLibrary");
    RescueLibrary::indexFileOverride() = root.getChildFile ("LibraryIndex.json");
    auto dir = root.getChildFile ("samples");
    dir.createDirectory();

    // One-shots and loops: the film analyses a full beat as a Loop source, so
    // the ranked suggestions have to be loop-shaped candidates, not only
    // kicks. Both kinds are generated and the matcher sorts them by profile.
    struct Demo { const char* name; double freq, seconds, decay; bool loop; };
    static const Demo demos[] = {
        { "Kick_Deep_01.wav",     52.0, 0.55, 6.0,  false },
        { "Kick_Punch_02.wav",    58.0, 0.40, 9.0,  false },
        { "Kick_Vintage_03.wav",  49.0, 0.70, 5.0,  false },
        { "Sub_808_Long.wav",     45.0, 2.20, 1.2,  false },
        { "Loop_Trapsoul_01.wav", 55.0, 3.20, 0.0,  true  },
        { "Loop_Nightdrive.wav",  49.0, 3.20, 0.0,  true  },
        { "Loop_Smooth_808.wav",  58.0, 3.20, 0.0,  true  },
        { "Loop_Dusty_Keys.wav",  62.0, 3.20, 0.0,  true  },
    };

    juce::WavAudioFormat wav;
    juce::Random rng (0x100b2);
    for (const auto& d : demos)
    {
        auto file = dir.getChildFile (d.name);
        if (file.existsAsFile())
            continue;
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), 48000.0, 1, 16, {}, 0));
        if (writer == nullptr)
            continue;
        stream.release();

        const int n = (int) (48000.0 * d.seconds);
        juce::AudioBuffer<float> b (1, n);
        double phase = 0.0, hatState = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / 48000.0;

            if (! d.loop)
            {
                const float env = (float) std::exp (-t * d.decay);
                phase += 2.0 * juce::MathConstants<double>::pi * (d.freq + 20.0 * env) / 48000.0;
                b.setSample (0, i, 0.85f * env * (float) std::sin (phase));
            }
            else
            {
                // A broadband bar: kick on the beat, hats between, a body
                // chord - so a Loop profile has something real to score.
                const double beat = 60.0 / 150.0;
                const double intoBeat = std::fmod (t, beat);
                const double intoEighth = std::fmod (t, beat * 0.5);
                const float kickEnv = (float) std::exp (-intoBeat * 20.0);
                phase += 2.0 * juce::MathConstants<double>::pi
                           * (d.freq + 30.0 * kickEnv) / 48000.0;
                const float kick = 0.75f * kickEnv * (float) std::sin (phase);

                const float hatEnv = (float) std::exp (-intoEighth * 80.0);
                const float noise = rng.nextFloat() * 2.0f - 1.0f;
                hatState += 0.55 * (noise - hatState);
                const float hat = (float) ((noise - hatState) * hatEnv * 0.18);

                const float body = 0.16f * (float) (
                      std::sin (2.0 * juce::MathConstants<double>::pi * d.freq * 4.0 * t)
                    + std::sin (2.0 * juce::MathConstants<double>::pi * d.freq * 6.0 * t));

                b.setSample (0, i, juce::jlimit (-1.0f, 1.0f, kick + hat + body));
            }
        }
        writer->writeFromAudioSampleBuffer (b, 0, n);
    }
    return dir;
}

// --- main --------------------------------------------------------------------

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    @autoreleasepool
    {
        // The flag is pulled out first and the positions read from what is
        // left: treating "reel" as positional is how MasterGlo's first reel
        // render silently came out with no soundtrack.
        juce::StringArray args;
        for (int i = 1; i < argc; ++i)
        {
            const juce::String a { argv[i] };
            if (a.equalsIgnoreCase ("reel"))
                reelMode = true;
            else if (a.isNotEmpty())
                args.add (a);
        }

        if (reelMode)
        {
            videoWidth = 1080;
            videoHeight = 1920;
        }

        const juce::String outPath = args.size() > 0 ? args[0]
                                                     : juce::String ("SourceGloPro-demo.mp4");
        const juce::File sourceAudio = args.size() > 1 ? juce::File (args[1]) : juce::File();
        const double sourceOffset = args.size() > 2 ? args[2].getDoubleValue() : 0.0;
        const double bitrateMbps  = args.size() > 3 ? args[3].getDoubleValue() : 14.0;

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader;
        if (sourceAudio.existsAsFile())
        {
            reader.reset (formats.createReaderFor (sourceAudio));
            if (reader == nullptr)
                std::printf ("could not read %s - falling back to the generated bed\n",
                             sourceAudio.getFullPathName().toRawUTF8());
        }
        else if (sourceAudio != juce::File())
        {
            std::printf ("no such file: %s\n", sourceAudio.getFullPathName().toRawUTF8());
            return 1;
        }

        const bool haveMusic = reader != nullptr;
        const double sr = haveMusic ? reader->sampleRate : 48000.0;
        const int blockSize = (int) std::llround (sr / fps);
        juce::int64 readPos = haveMusic ? (juce::int64) (sourceOffset * sr) : 0;

        if (haveMusic)
            std::printf ("source: %s\n  %.0f Hz, %d ch, %.1f s\n",
                         sourceAudio.getFileName().toRawUTF8(), sr,
                         (int) reader->numChannels,
                         (double) reader->lengthInSamples / sr);

        const auto demoDir = buildDemoLibrary();

        SourceGloProcessor processor;
        processor.setPlayConfigDetails (2, 2, sr, blockSize);
        processor.prepareToPlay (sr, blockSize);

        processor.getLibrary().addFolder (demoDir);
        for (int i = 0; i < 250 && processor.getLibrary().isScanning(); ++i)
            juce::Thread::sleep (20);
        processor.refreshRescues();

        std::unique_ptr<juce::AudioProcessorEditor> editorHolder (processor.createEditorIfNeeded());
        auto* editor = dynamic_cast<SourceGloEditor*> (editorHolder.get());
        if (editor == nullptr)
        {
            std::printf ("could not create the editor\n");
            return 1;
        }
        editor->setSize (panelWidth, panelHeight);

        // --- the script -------------------------------------------------------
        const std::vector<Segment> timeline =
        {
            // Logo opener.
            { 0.0, 5.5, {}, {},
              [] (SourceGloProcessor& p, SourceGloEditor& e, double)
              {
                  e.showAnalysisTab (0);
                  loadPreset (p, "Loop Glue Fast");
                  setParam (p, pid::sourceType, 8.0f);      // a full beat is a Loop
              } },

            { 5.5, 11.0, "Know before you build",
              "Your mix is only as good as what you feed it", nullptr },

            { 11.0, 20.0, "Analyze",
              "Pick the source type, play it, press Analyze",
              [] (SourceGloProcessor& p, SourceGloEditor&, double progress)
              {
                  if (progress > 0.45 && ! p.getAnalysis().analyzed)
                      p.analyzeNow();
              } },

            { 20.0, 28.5, "Source Score",
              juce::String::fromUTF8 ("Tone · Punch · Level · Phase · Fit — five pods, one number"),
              nullptr },

            { 28.5, 37.0, "Diagnostics",
              "It names the problem: masking, headroom, phase, mud", nullptr },

            { 37.0, 49.0, "Fix Source",
              "One button applies the correction the analysis computed",
              [] (SourceGloProcessor& p, SourceGloEditor&, double progress)
              {
                  if (! p.isFixEngaged() && progress > 0.15)
                  {
                      p.requestFixSource();
                      p.analyzeNow();
                  }
                  // Ride the amount so the correction is seen arriving, and
                  // re-score along the way: analysis measures through the
                  // chain, so the pods climb as the fix deepens.
                  if (progress > 0.35)
                  {
                      const float ride = (float) juce::jlimit (0.0, 1.0, (progress - 0.35) / 0.5);
                      setParam (p, pid::fixAmount, 20.0f + ride * 80.0f);

                      static const double marks[] = { 0.52, 0.68, 0.86 };
                      for (double mark : marks)
                          if (progress > mark && progress < mark + 0.035)
                              p.analyzeNow();
                  }
              } },

            { 49.0, 61.0, "Eight macros",
              juce::String::fromUTF8 ("Sub · Punch · Body · Tone · Air · Stereo · Transients · Saturate"),
              [] (SourceGloProcessor& p, SourceGloEditor&, double progress)
              {
                  // Each macro takes the stage in turn, returning to its
                  // preset value as the next one moves.
                  struct Move { const char* id; float rest, peak; };
                  static const Move moves[] = {
                      { pid::sub,        20.0f, 78.0f },
                      { pid::punch,      60.0f, 95.0f },
                      { pid::body,       55.0f, 92.0f },
                      { pid::tone,       50.0f, 12.0f },
                      { pid::air,        40.0f, 88.0f },
                      { pid::stereo,     20.0f, 85.0f },
                      { pid::transients, 65.0f, 98.0f },
                      { pid::saturate,   35.0f, 90.0f },
                  };
                  const int count = (int) (sizeof (moves) / sizeof (moves[0]));
                  const double each = 1.0 / count;

                  for (int i = 0; i < count; ++i)
                  {
                      const double local = (progress - i * each) / each;
                      const float bump = (local >= 0.0 && local <= 1.0)
                          ? (float) (0.5 - 0.5 * std::cos (local * 2.0
                                        * juce::MathConstants<double>::pi))
                          : 0.0f;
                      setParam (p, moves[i].id,
                                moves[i].rest + (moves[i].peak - moves[i].rest) * bump);
                  }
              } },

            { 61.0, 70.0, "Fit",
              "Band balance against the target for that exact source",
              [] (SourceGloProcessor& p, SourceGloEditor& e, double progress)
              {
                  e.showAnalysisTab (1);
                  if (progress > 0.1 && progress < 0.13)
                      p.analyzeNow();
              } },

            { 70.0, 79.0, "Detail",
              "Every measurement, and how the score is built",
              [] (SourceGloProcessor&, SourceGloEditor& e, double)
              {
                  e.showAnalysisTab (3);
              } },

            { 79.0, 90.0, "Rescue",
              "When it cannot be saved: ranked matches from your own library",
              [] (SourceGloProcessor&, SourceGloEditor& e, double)
              {
                  e.showAnalysisTab (2);
              } },

            { 90.0, 97.0, "Library",
              "Point it at your packs once - it works in every session",
              [] (SourceGloProcessor&, SourceGloEditor& e, double)
              {
                  e.showAnalysisTab (4);
              } },

            { 97.0, 108.0, "Presets",
              "29 factory presets by source type, and your own",
              [] (SourceGloProcessor& p, SourceGloEditor& e, double progress)
              {
                  e.showAnalysisTab (0);

                  static const char* names[] = { "Loop Tape Warmth", "Deep 808 Control",
                                                 "Loop Wide & Bright", "Vocal Clarity Rescue",
                                                 "Punchy Kick Starter" };
                  const int count = (int) (sizeof (names) / sizeof (names[0]));
                  const int index = juce::jlimit (0, count - 1, (int) (progress * count));

                  static int lastIndex = -1;
                  if (index != lastIndex)
                  {
                      loadPreset (p, names[index]);
                      p.analyzeNow();
                      lastIndex = index;
                  }
              } },

            { 108.0, 116.0, {}, {}, nullptr },      // logo closer
        };

        // A reel earns attention in the first second or loses it: six acts in
        // 44 s, each with the panel region it is talking about blown up in the
        // lower half. Focus rects are in the editor's 1491x1055 canvas.
        const std::vector<Segment> reelTimeline =
        {
            { 0.0, 3.4, {}, {},
              [] (SourceGloProcessor& p, SourceGloEditor& e, double)
              {
                  e.showAnalysisTab (0);
                  loadPreset (p, "Loop Glue Fast");
                  setParam (p, pid::sourceType, 8.0f);
              }, {} },

            { 3.4, 10.5, "Score any source",
              "0-100 against a modern pro standard",
              [] (SourceGloProcessor& p, SourceGloEditor&, double progress)
              {
                  if (progress > 0.35 && ! p.getAnalysis().analyzed)
                      p.analyzeNow();
              },
              { 560.0f, 78.0f, 500.0f, 420.0f } },       // the score ring

            { 10.5, 17.5, "It names the problem",
              "Masking, headroom, phase, mud - in plain language",
              nullptr,
              { 1030.0f, 78.0f, 440.0f, 430.0f } },      // diagnostics column

            { 17.5, 26.5, "One button fixes it",
              "The correction the analysis computed, scaled to taste",
              [] (SourceGloProcessor& p, SourceGloEditor&, double progress)
              {
                  if (! p.isFixEngaged() && progress > 0.12)
                  {
                      p.requestFixSource();
                      p.analyzeNow();
                  }
                  if (progress > 0.3)
                  {
                      const float ride = (float) juce::jlimit (0.0, 1.0, (progress - 0.3) / 0.55);
                      setParam (p, pid::fixAmount, 20.0f + ride * 80.0f);
                      static const double marks[] = { 0.5, 0.7, 0.88 };
                      for (double mark : marks)
                          if (progress > mark && progress < mark + 0.04)
                              p.analyzeNow();
                  }
              },
              { 350.0f, 440.0f, 660.0f, 90.0f } },       // the three buttons

            { 26.5, 34.5, "Eight macros",
              juce::String::fromUTF8 ("Sub · Punch · Body · Tone · Air · Stereo · Transients · Saturate"),
              [] (SourceGloProcessor& p, SourceGloEditor&, double progress)
              {
                  struct Move { const char* id; float rest, peak; };
                  static const Move moves[] = {
                      { pid::sub,        15.0f, 80.0f },
                      { pid::punch,      45.0f, 95.0f },
                      { pid::body,       50.0f, 90.0f },
                      { pid::air,        40.0f, 88.0f },
                      { pid::stereo,     30.0f, 85.0f },
                      { pid::saturate,   40.0f, 92.0f },
                  };
                  const int count = (int) (sizeof (moves) / sizeof (moves[0]));
                  const double each = 1.0 / count;
                  for (int i = 0; i < count; ++i)
                  {
                      const double local = (progress - i * each) / each;
                      const float bump = (local >= 0.0 && local <= 1.0)
                          ? (float) (0.5 - 0.5 * std::cos (local * 2.0
                                        * juce::MathConstants<double>::pi))
                          : 0.0f;
                      setParam (p, moves[i].id,
                                moves[i].rest + (moves[i].peak - moves[i].rest) * bump);
                  }
              },
              { 20.0f, 855.0f, 1000.0f, 150.0f } },      // the macro row

            { 34.5, 44.0, "Or replace it",
              "Ranked matches from your own sample library",
              [] (SourceGloProcessor&, SourceGloEditor& e, double)
              {
                  e.showAnalysisTab (2);
              },
              { 1017.0f, 528.0f, 464.0f, 441.0f } },     // rescue panel

            { 44.0, 51.0, {}, {}, nullptr, {} },         // logo closer
        };

        const auto& script = reelMode ? reelTimeline : timeline;
        const double duration = script.back().end;
        const int totalFrames = (int) (duration * fps);

        // --- writer ------------------------------------------------------------
        auto toNS = [] (const juce::String& str)
        {
            return [NSString stringWithUTF8String: str.toRawUTF8()];
        };

        const auto finalFile = juce::File::getCurrentWorkingDirectory().getChildFile (outPath);
        const auto videoOnlyFile = finalFile.getSiblingFile (finalFile.getFileNameWithoutExtension()
                                                               + "-videoonly.mp4");
        const auto wavFile = finalFile.getSiblingFile (finalFile.getFileNameWithoutExtension()
                                                          + "-audio.wav");
        const auto m4aFile = finalFile.getSiblingFile (finalFile.getFileNameWithoutExtension()
                                                          + "-audio.m4a");

        auto* url = [NSURL fileURLWithPath: toNS (videoOnlyFile.getFullPathName())];
        [[NSFileManager defaultManager] removeItemAtURL: url error: nil];

        NSError* error = nil;
        AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL: url
                                                         fileType: AVFileTypeMPEG4
                                                            error: &error];
        if (writer == nil)
        {
            std::printf ("could not create the writer: %s\n",
                         error.localizedDescription.UTF8String);
            return 1;
        }

        NSDictionary* settings = @{
            AVVideoCodecKey: AVVideoCodecTypeH264,
            AVVideoWidthKey: @(videoWidth),
            AVVideoHeightKey: @(videoHeight),
            AVVideoCompressionPropertiesKey: @{
                AVVideoAverageBitRateKey: @((int) (bitrateMbps * 1000000.0)),
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
                AVVideoMaxKeyFrameIntervalKey: @(fps * 2),
            },
        };

        AVAssetWriterInput* input = [AVAssetWriterInput assetWriterInputWithMediaType: AVMediaTypeVideo
                                                                      outputSettings: settings];
        input.expectsMediaDataInRealTime = NO;

        NSDictionary* bufferAttributes = @{
            (NSString*) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (NSString*) kCVPixelBufferWidthKey: @(videoWidth),
            (NSString*) kCVPixelBufferHeightKey: @(videoHeight),
        };
        auto* adaptor = [AVAssetWriterInputPixelBufferAdaptor
                           assetWriterInputPixelBufferAdaptorWithAssetWriterInput: input
                                                      sourcePixelBufferAttributes: bufferAttributes];

        [writer addInput: input];
        [writer startWriting];
        [writer startSessionAtSourceTime: kCMTimeZero];

        // --- render ------------------------------------------------------------
        DemoSignal signal (sr);
        juce::AudioBuffer<float> audio (2, blockSize);
        juce::MidiBuffer midi;

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> audioWriter;

        if (haveMusic)
        {
            wavFile.deleteFile();
            if (auto stream = std::unique_ptr<juce::OutputStream> (wavFile.createOutputStream()))
            {
                const auto options = juce::AudioFormatWriterOptions{}
                                       .withSampleRate (sr)
                                       .withNumChannels (2)
                                       .withBitsPerSample (24);
                audioWriter = wavFormat.createWriterFor (stream, options);
            }
        }

        juce::Image frame (juce::Image::ARGB, videoWidth, videoHeight, true);
        juce::Image panel (juce::Image::ARGB, panelWidth, panelHeight, true);

        std::printf ("rendering %d frames (%.0f seconds) at %dx%d\n",
                     totalFrames, duration, videoWidth, videoHeight);

        for (int f = 0; f < totalFrames; ++f)
        {
            const double t = (double) f / fps;

            const Segment* current = nullptr;
            for (const auto& seg : script)
                if (t >= seg.start && t < seg.end)
                    current = &seg;

            if (current != nullptr && current->action != nullptr)
                current->action (processor, *editor,
                                 (t - current->start) / (current->end - current->start));

            if (haveMusic)
            {
                if (readPos + blockSize >= reader->lengthInSamples)
                    readPos = 0;
                reader->read (&audio, 0, blockSize, readPos, true, true);
                readPos += blockSize;
            }
            else
            {
                signal.render (audio);
            }

            processor.processBlock (audio, midi);

            // The soundtrack is the PROCESSED output: what the plugin did to
            // the audio the viewer is watching it analyse.
            if (audioWriter != nullptr)
                audioWriter->writeFromAudioSampleBuffer (audio, 0, blockSize);
            editor->refreshDisplays();

            // --- compose -------------------------------------------------------
            {
                juce::Graphics g (frame);
                drawBackdrop (g);

                const float panelAlpha = reelMode ? envelopeFor (t, 2.9, 44.4, 0.9, 0.8)
                                                  : envelopeFor (t, 4.6, 109.0, 1.2, 1.0);

                if (panelAlpha > 0.01f)
                {
                    { juce::Graphics pg (panel); editor->paintEntireComponent (pg, true); }

                    const float rise = (1.0f - panelAlpha) * 26.0f;

                    if (reelMode)
                    {
                        auto full = juce::Rectangle<float> (reel::panelW, reel::panelH)
                                      .withCentre ({ (float) videoWidth * 0.5f,
                                                     reel::panelY + reel::panelH * 0.5f + rise });

                        g.setColour (juce::Colours::black.withAlpha (0.5f * panelAlpha));
                        g.fillRoundedRectangle (full.expanded (12.0f), 18.0f);

                        g.setOpacity (panelAlpha);
                        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                        g.drawImage (panel, full, juce::RectanglePlacement::stretchToFit, false);
                        g.setOpacity (1.0f);

                        // ...and the act's own region blown up underneath, so
                        // the lower half is real interface, not backdrop.
                        if (current != nullptr)
                            drawFocus (g, panel, current->focus,
                                       juce::Rectangle<float> (0.0f, reel::detailY,
                                                               (float) videoWidth, reel::detailH),
                                       panelAlpha);

                        // Small wordmark riding the top band once the intro is over.
                        drawLogo (g, 0.9f * panelAlpha, 0.60f, reel::logoY + reel::logoH * 0.5f);
                    }
                    else
                    {
                        // 1244 wide keeps the native 1491x1055 canvas' aspect
                        // and leaves room for the title and caption bands.
                        auto target = juce::Rectangle<float> (1244.0f, 880.0f)
                                        .withCentre ({ (float) videoWidth * 0.5f, 512.0f + rise });

                        g.setColour (juce::Colours::black.withAlpha (0.55f * panelAlpha));
                        g.fillRoundedRectangle (target.expanded (16.0f), 24.0f);

                        g.setOpacity (panelAlpha);
                        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                        g.drawImage (panel, target, juce::RectanglePlacement::centred, false);
                        g.setOpacity (1.0f);
                    }
                }

                // Logo opener: the mark rises alone, then cross-dissolves into
                // the full lockup. The wordmark asset ALREADY contains the
                // mark, so drawing both at once reads as a duplicated logo -
                // they must never overlap.
                const float centreY = (float) videoHeight * 0.5f;

                const float markAlpha = reelMode ? envelopeFor (t, 0.1, 1.9, 0.8, 0.5)
                                                 : envelopeFor (t, 0.15, 2.5, 1.0, 0.55);
                if (markAlpha > 0.01f)
                    drawMark (g, markAlpha * 0.95f,
                              (reelMode ? 300.0f : 270.0f) + 26.0f * markAlpha,
                              { (float) videoWidth * 0.5f, centreY });

                const float introAlpha = reelMode ? envelopeFor (t, 1.7, 3.4, 0.7, 0.6)
                                                  : envelopeFor (t, 2.25, 4.9, 0.85, 0.8);
                if (introAlpha > 0.01f)
                {
                    drawLogo (g, introAlpha, 0.97f + 0.03f * introAlpha, centreY);
                    g.setColour (tokens::cyan.withAlpha (0.85f * introAlpha));
                    drawTracked (g, "PRODUCTION INTELLIGENCE FOR BETTER MIXES",
                                 juce::Rectangle<float> (0.0f, centreY + 128.0f,
                                                         (float) videoWidth, 44.0f),
                                 Fonts::make (reelMode ? 20.0f : 25.0f), 5.0f);
                }

                // Logo closer.
                const float outroAlpha = reelMode ? envelopeFor (t, 43.8, 51.0, 0.9, 1.0)
                                                  : envelopeFor (t, 108.4, 116.0, 1.1, 1.2);
                if (outroAlpha > 0.01f)
                {
                    drawLogo (g, outroAlpha, 1.0f, centreY);

                    g.setColour (tokens::text.withAlpha (0.88f * outroAlpha));
                    // fromUTF8, not a bare literal: juce::String reads the middle
                    // dot's two UTF-8 bytes as Latin-1 and draws "Â·".
                    drawTracked (g, juce::String::fromUTF8 (reelMode ? "VST3  ·  AU  ·  STANDALONE"
                                                    : "VST3  ·  AUDIO UNIT  ·  STANDALONE  ·  MACOS"),
                                 juce::Rectangle<float> (0.0f, centreY + 124.0f,
                                                         (float) videoWidth, 44.0f),
                                 Fonts::make (reelMode ? 21.0f : 24.0f), 4.0f);
                    g.setColour (tokens::gold.withAlpha (0.85f * outroAlpha));
                    drawTracked (g, "DIAMOND LOOPZ",
                                 juce::Rectangle<float> (0.0f, centreY + 192.0f,
                                                         (float) videoWidth, 40.0f),
                                 Fonts::make (reelMode ? 19.0f : 21.0f), 6.0f);
                }

                if (current != nullptr)
                {
                    const float fade = envelopeFor (t, current->start, current->end,
                                                    reelMode ? 0.45 : 0.6,
                                                    reelMode ? 0.45 : 0.6);
                    drawTitle (g, current->title, fade,
                               reelMode ? juce::Rectangle<float> (0.0f, reel::titleY,
                                                                  (float) videoWidth, reel::titleH)
                                        : juce::Rectangle<float> (0.0f, 26.0f,
                                                                  (float) videoWidth, 74.0f),
                               reelMode ? 44.0f : 50.0f, false);
                    drawCaption (g, current->caption, fade,
                                 reelMode ? juce::Rectangle<float> (0.0f, reel::captionY,
                                                                    (float) videoWidth, reel::captionH)
                                          : juce::Rectangle<float> (0.0f, 992.0f,
                                                                    (float) videoWidth, 60.0f),
                                 reelMode ? 23.0f : 27.0f);
                }
            }

            // A still per act, so a render can be reviewed without scrubbing -
            // and a bad overlay is caught here rather than after upload.
            {
                static const std::array<double, 14> filmStills
                    { 1.6, 3.6, 8.0, 16.0, 24.0, 33.0, 47.6, 56.0, 65.0, 74.0,
                      84.0, 93.0, 102.0, 112.0 };
                static const std::array<double, 8> reelStills
                    { 1.2, 2.8, 7.0, 14.0, 24.0, 31.0, 40.0, 47.5 };

                std::vector<double> stillTimes;
                if (reelMode) stillTimes.assign (reelStills.begin(), reelStills.end());
                else          stillTimes.assign (filmStills.begin(), filmStills.end());

                for (double mark : stillTimes)
                    if (std::abs (t - mark) < 0.5 / fps)
                    {
                        auto dir = juce::File::getCurrentWorkingDirectory()
                                    .getChildFile (reelMode ? "reel-stills" : "video-stills");
                        dir.createDirectory();
                        auto still = dir.getChildFile ("still-" + juce::String (mark, 1) + "s.png");
                        still.deleteFile();
                        juce::PNGImageFormat png;
                        if (auto out = std::unique_ptr<juce::FileOutputStream> (still.createOutputStream()))
                            png.writeImageToStream (frame, *out);
                    }
            }

            if (! appendFrame (adaptor, input, frame,
                               CMTimeMake ((int64_t) f, (int32_t) fps)))
            {
                std::printf ("frame %d failed to encode\n", f);
                return 1;
            }

            if (f % (fps * 5) == 0)
                std::printf ("  %3d%%  (%.0fs)\n", (f * 100) / totalFrames, t);
        }

        [input markAsFinished];

        __block bool finished = false;
        [writer finishWritingWithCompletionHandler: ^{ finished = true; }];
        while (! finished)
            [NSThread sleepForTimeInterval: 0.05];

        if (writer.status != AVAssetWriterStatusCompleted)
        {
            std::printf ("writer failed: %s\n",
                         writer.error.localizedDescription.UTF8String);
            return 1;
        }

        audioWriter.reset();

        // --- mux ---------------------------------------------------------------
        if (haveMusic && wavFile.existsAsFile())
        {
            std::printf ("\nencoding the soundtrack and muxing...\n");

            // afconvert ships with macOS, so the AAC encode needs no install.
            // Encoding first lets the mux be a passthrough - the video is never
            // recompressed.
            m4aFile.deleteFile();
            juce::ChildProcess convert;
            convert.start (juce::StringArray { "/usr/bin/afconvert", "-f", "m4af",
                                               "-d", "aac", "-b", "256000",
                                               wavFile.getFullPathName(),
                                               m4aFile.getFullPathName() });
            convert.waitForProcessToFinish (180000);

            if (! m4aFile.existsAsFile())
            {
                std::printf ("afconvert failed; leaving the silent video in place\n");
                videoOnlyFile.moveFileTo (finalFile);
            }
            else
            {
                auto* composition = [AVMutableComposition composition];
                auto* videoAsset = [AVURLAsset URLAssetWithURL:
                                      [NSURL fileURLWithPath: toNS (videoOnlyFile.getFullPathName())]
                                                      options: nil];
                auto* audioAsset = [AVURLAsset URLAssetWithURL:
                                      [NSURL fileURLWithPath: toNS (m4aFile.getFullPathName())]
                                                      options: nil];

                auto* videoTrack = [composition addMutableTrackWithMediaType: AVMediaTypeVideo
                                                            preferredTrackID: kCMPersistentTrackID_Invalid];
                auto* audioTrack = [composition addMutableTrackWithMediaType: AVMediaTypeAudio
                                                            preferredTrackID: kCMPersistentTrackID_Invalid];

                NSError* muxError = nil;
                const CMTimeRange range = CMTimeRangeMake (kCMTimeZero, videoAsset.duration);

                [videoTrack insertTimeRange: range
                                    ofTrack: [videoAsset tracksWithMediaType: AVMediaTypeVideo].firstObject
                                     atTime: kCMTimeZero error: &muxError];
                [audioTrack insertTimeRange: range
                                    ofTrack: [audioAsset tracksWithMediaType: AVMediaTypeAudio].firstObject
                                     atTime: kCMTimeZero error: &muxError];

                finalFile.deleteFile();
                auto* session = [[AVAssetExportSession alloc]
                                   initWithAsset: composition
                                      presetName: AVAssetExportPresetPassthrough];
                session.outputURL = [NSURL fileURLWithPath: toNS (finalFile.getFullPathName())];
                session.outputFileType = AVFileTypeMPEG4;

                __block bool muxed = false;
                [session exportAsynchronouslyWithCompletionHandler: ^{ muxed = true; }];
                while (! muxed)
                    [NSThread sleepForTimeInterval: 0.05];

                if (session.status != AVAssetExportSessionStatusCompleted)
                {
                    std::printf ("mux failed: %s\n",
                                 session.error.localizedDescription.UTF8String);
                    videoOnlyFile.moveFileTo (finalFile);
                }
                else
                {
                    videoOnlyFile.deleteFile();
                    wavFile.deleteFile();
                    m4aFile.deleteFile();
                }
            }
        }
        else
        {
            videoOnlyFile.moveFileTo (finalFile);
        }

        std::printf ("\nwrote %s\n  %.1f MB, %.0f seconds, %dx%d @ %d fps, %s\n",
                     finalFile.getFullPathName().toRawUTF8(),
                     finalFile.getSize() / (1024.0 * 1024.0), duration,
                     videoWidth, videoHeight, fps,
                     haveMusic ? "with the processed soundtrack" : "silent");

        editorHolder.reset();
        processor.editorBeingDeleted (nullptr);
    }
    return 0;
}
