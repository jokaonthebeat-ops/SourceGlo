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
static constexpr int videoWidth = 1920;
static constexpr int videoHeight = 1080;

// The editor renders at its native design canvas and is downscaled into the
// frame - crisper than rendering small, and the aspect never has to be guessed.
static constexpr int panelWidth = 1491;
static constexpr int panelHeight = 1055;

// --- helpers -----------------------------------------------------------------

struct Segment
{
    double start, end;
    juce::String title;
    juce::String caption;
    std::function<void (SourceGloProcessor&, SourceGloEditor&, double progress)> action;
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
    const float w = (float) videoWidth * 0.52f * scale;
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

    struct Demo { const char* name; double freq, seconds, decay; };
    static const Demo demos[] = {
        { "Kick_Deep_01.wav",    52.0, 0.55, 6.0 },
        { "Kick_Punch_02.wav",   58.0, 0.40, 9.0 },
        { "Kick_Vintage_03.wav", 49.0, 0.70, 5.0 },
        { "Kick_Tight_04.wav",   63.0, 0.30, 12.0 },
        { "Sub_808_Long.wav",    45.0, 2.20, 1.2 },
        { "Kick_Modern_05.wav",  55.0, 0.48, 7.5 },
    };

    juce::WavAudioFormat wav;
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
        double phase = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / 48000.0;
            const float env = (float) std::exp (-t * d.decay);
            phase += 2.0 * juce::MathConstants<double>::pi * (d.freq + 20.0 * env) / 48000.0;
            b.setSample (0, i, 0.85f * env * (float) std::sin (phase));
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
        juce::StringArray args;
        for (int i = 1; i < argc; ++i)
            args.add (juce::String (argv[i]));

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
                  loadPreset (p, "Punchy Kick Starter");
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
                  // Ride the amount so the correction is seen arriving.
                  if (progress > 0.35)
                  {
                      const float ride = (float) juce::jlimit (0.0, 1.0, (progress - 0.35) / 0.5);
                      setParam (p, pid::fixAmount, 20.0f + ride * 80.0f);
                      if (progress > 0.8 && progress < 0.83)
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

                  static const char* names[] = { "Deep 808 Control", "Snare Snap Doctor",
                                                 "Vocal Clarity Rescue", "Loop Tape Warmth",
                                                 "Tight Kick Snap" };
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

        const double duration = timeline.back().end;
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
            for (const auto& seg : timeline)
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

                const float panelAlpha = envelopeFor (t, 4.6, 109.0, 1.2, 1.0);

                if (panelAlpha > 0.01f)
                {
                    { juce::Graphics pg (panel); editor->paintEntireComponent (pg, true); }

                    const float rise = (1.0f - panelAlpha) * 26.0f;
                    // 1244 wide keeps the native 1491x1055 canvas' aspect and
                    // leaves room for the title band and the caption rule.
                    auto target = juce::Rectangle<float> (1244.0f, 880.0f)
                                    .withCentre ({ (float) videoWidth * 0.5f, 512.0f + rise });

                    g.setColour (juce::Colours::black.withAlpha (0.55f * panelAlpha));
                    g.fillRoundedRectangle (target.expanded (16.0f), 24.0f);

                    g.setOpacity (panelAlpha);
                    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                    g.drawImage (panel, target, juce::RectanglePlacement::centred, false);
                    g.setOpacity (1.0f);
                }

                // Logo opener: the mark rises, then the wordmark and tagline.
                const float markAlpha = envelopeFor (t, 0.1, 4.4, 1.0, 0.7);
                if (markAlpha > 0.01f)
                    drawMark (g, markAlpha * 0.95f, 250.0f + 24.0f * markAlpha,
                              { (float) videoWidth * 0.5f, 372.0f });

                const float introAlpha = envelopeFor (t, 0.55, 4.9, 1.1, 0.8);
                if (introAlpha > 0.01f)
                {
                    drawLogo (g, introAlpha, 0.95f + 0.05f * introAlpha, 604.0f);
                    g.setColour (tokens::cyan.withAlpha (0.85f * introAlpha));
                    drawTracked (g, "PRODUCTION INTELLIGENCE FOR BETTER MIXES",
                                 juce::Rectangle<float> (0.0f, 706.0f, (float) videoWidth, 44.0f),
                                 Fonts::make (25.0f), 5.0f);
                }

                // Logo closer.
                const float outroAlpha = envelopeFor (t, 108.4, 116.0, 1.1, 1.2);
                if (outroAlpha > 0.01f)
                {
                    drawMark (g, outroAlpha * 0.9f, 210.0f,
                              { (float) videoWidth * 0.5f, 352.0f });
                    drawLogo (g, outroAlpha, 1.0f, 566.0f);

                    g.setColour (tokens::text.withAlpha (0.88f * outroAlpha));
                    // fromUTF8, not a bare literal: juce::String reads the middle
                    // dot's two UTF-8 bytes as Latin-1 and draws "Â·".
                    drawTracked (g, juce::String::fromUTF8 ("VST3  ·  AUDIO UNIT  ·  STANDALONE  ·  MACOS"),
                                 juce::Rectangle<float> (0.0f, 662.0f, (float) videoWidth, 44.0f),
                                 Fonts::make (24.0f), 4.0f);
                    g.setColour (tokens::gold.withAlpha (0.85f * outroAlpha));
                    drawTracked (g, "DIAMOND LOOPZ",
                                 juce::Rectangle<float> (0.0f, 732.0f, (float) videoWidth, 40.0f),
                                 Fonts::make (21.0f), 6.0f);
                }

                if (current != nullptr)
                {
                    drawTitle (g, current->title,
                               envelopeFor (t, current->start, current->end, 0.6, 0.6),
                               juce::Rectangle<float> (0.0f, 26.0f, (float) videoWidth, 74.0f),
                               50.0f, false);
                    drawCaption (g, current->caption,
                                 envelopeFor (t, current->start, current->end, 0.5, 0.5),
                                 juce::Rectangle<float> (0.0f, 992.0f, (float) videoWidth, 60.0f));
                }
            }

            // A still per act, so a render can be reviewed without scrubbing -
            // and a bad overlay is caught here rather than after upload.
            {
                static const std::array<double, 13> stillTimes
                    { 2.4, 8.0, 16.0, 24.0, 33.0, 45.0, 56.0, 65.0, 74.0,
                      84.0, 93.0, 102.0, 112.0 };
                for (double mark : stillTimes)
                    if (std::abs (t - mark) < 0.5 / fps)
                    {
                        auto dir = juce::File::getCurrentWorkingDirectory()
                                    .getChildFile ("video-stills");
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
