// -----------------------------------------------------------------------------
//  Renders the SourceGlo Pro editor to a PNG without opening a window.
//
//    make uishot                            -> build/SourceGlo-ui.png at 1491x1055
//    make uishot ARGS="out.png min"         -> 1044x739
//    make uishot ARGS="out.png max"         -> 2237x1583
//    make uishot ARGS="out.png def signal"  -> feed test audio first so the
//                                              meters and spectrum run live
//
//  Reports artwork that failed to load - the difference between "the design
//  is wrong" and "the install is wrong".
// -----------------------------------------------------------------------------

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <cstdio>

using namespace sourceglo;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String outName = argc > 1 ? argv[1] : "SourceGlo-ui.png";
    const juce::String sizeArg = argc > 2 ? juce::String (argv[2]).toLowerCase() : "def";
    juce::String modeArg = argc > 3 ? juce::String (argv[3]).toLowerCase() : "";
    const bool wantFix = modeArg == "fix";      // signal + engage the correction
    if (wantFix)
        modeArg = "signal";
    const int wantTab = argc > 4 ? juce::String (argv[4]).getIntValue() : 0;

    int width = Design::width, height = Design::height;
    if (sizeArg == "min")      { width = Design::minWidth; height = Design::minHeight; }
    else if (sizeArg == "max") { width = Design::maxWidth; height = Design::maxHeight; }
    else if (sizeArg.containsChar ('x'))
    {
        width  = sizeArg.upToFirstOccurrenceOf ("x", false, false).getIntValue();
        height = sizeArg.fromFirstOccurrenceOf ("x", false, false).getIntValue();
    }

    // Signal shots build a small generated demo library in a scratch
    // location (never the user's real index) so the rescue rows and the
    // Library tab show the feature live.
    if (modeArg == "signal")
    {
        auto demoRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("SourceGloDemoLibrary");
        RescueLibrary::indexFileOverride() = demoRoot.getChildFile ("LibraryIndex.json");
        auto sampleDir = demoRoot.getChildFile ("samples");
        sampleDir.createDirectory();

        struct Demo { const char* name; double freq, seconds, decay; };
        const Demo demos[] = {
            { "Kick_Deep_01.wav",   52.0, 0.55, 6.0 },
            { "Kick_Punch_02.wav",  58.0, 0.40, 9.0 },
            { "Kick_Vintage_03.wav",49.0, 0.70, 5.0 },
            { "Kick_Tight_04.wav",  63.0, 0.30, 12.0 },
            { "Sub_808_Long.wav",   45.0, 2.20, 1.2 },
        };

        juce::WavAudioFormat wav;
        for (const auto& d : demos)
        {
            auto file = sampleDir.getChildFile (d.name);
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
    }

    SourceGloProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    if (modeArg == "signal")
    {
        auto sampleDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("SourceGloDemoLibrary/samples");
        processor.getLibrary().addFolder (sampleDir);
        for (int i = 0; i < 200 && processor.getLibrary().isScanning(); ++i)
            juce::Thread::sleep (20);
        processor.refreshRescues();
    }

    // Match the approved reference in signal mode: source type Kick. Set
    // before the editor exists so the dropdown constructs with it.
    if (modeArg == "signal")
        if (auto* param = processor.getAPVTS().getParameter (pid::sourceType))
            param->setValueNotifyingHost (param->convertTo0to1 (1.0f));

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
    {
        std::printf ("FAIL: createEditor returned null\n");
        return 2;
    }

    editor->setSize (width, height);
    auto* sgEditor = dynamic_cast<SourceGloEditor*> (editor.get());

    if (modeArg == "signal")
    {
        // Kick-flavoured test feed: 55 Hz tone bursts + filtered noise, enough
        // for the meters and the FFT path to display something honest.
        const double sr = 48000.0;
        const int blockSize = 512;
        const int blocks = (int) (sr * 5.0 / blockSize);

        juce::AudioBuffer<float> audio (2, blockSize);
        juce::MidiBuffer midi;
        juce::Random random (0x50617);
        double phase = 0.0;
        float lp = 0.0f;
        int sampleIndex = 0;

        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double beatPos = std::fmod ((double) sampleIndex / sr, 0.469); // 128 BPM
                const float env = (float) std::exp (-beatPos * 9.0);

                phase += 2.0 * juce::MathConstants<double>::pi * (55.0 + 25.0 * env) / sr;
                const float kick = 0.85f * env * (float) std::sin (phase);

                const float white = random.nextFloat() * 2.0f - 1.0f;
                lp += 0.15f * (white - lp);
                const float bed = 0.10f * lp;

                audio.setSample (0, i, kick + bed);
                audio.setSample (1, i, kick + bed * 0.9f);
                ++sampleIndex;
            }
            processor.processBlock (audio, midi);

            if (sgEditor != nullptr && block % 3 == 0)
                sgEditor->refreshDisplays();
        }

        // Real analysis of the captured test feed - the shot shows the
        // engine's actual verdict on it.
        processor.analyzeNow();

        if (wantFix)
        {
            processor.requestFixSource();
            processor.analyzeNow();     // publish the post-fix scores headlessly
        }
    }

    if (sgEditor != nullptr && wantTab > 0)
        sgEditor->showAnalysisTab (wantTab);

    // "menu" as the tab argument opens the preset browser instead, so the
    // chooser can be checked without rendering a whole film.
    if (sgEditor != nullptr && argc > 4 && juce::String (argv[4]) == "menu")
        sgEditor->openPresetBrowser();
    if (sgEditor != nullptr && argc > 4 && juce::String (argv[4]) == "types")
        sgEditor->openSourceTypeMenu();

    // Let the animated displays settle (score climb, spectrum smoothing).
    if (sgEditor != nullptr)
    {
        for (int i = 0; i < 45; ++i)
        {
            juce::Thread::sleep (34);
            sgEditor->refreshDisplays();
        }
    }

    // Top the meters back up so the capture shows them lit (they decay
    // during the settle loop).
    if (modeArg == "signal")
    {
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        juce::Random random (0x51);
        double phase = 0.0;
        for (int block = 0; block < 8; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * 58.0 / 48000.0;
                const float v = 0.13f * (float) std::sin (phase)
                              + 0.05f * (random.nextFloat() * 2.0f - 1.0f);
                audio.setSample (0, i, v);
                audio.setSample (1, i, v * 0.94f);
            }
            processor.processBlock (audio, midi);
        }
        if (sgEditor != nullptr)
        {
            juce::Thread::sleep (34);
            sgEditor->refreshDisplays();
        }
    }

    juce::Image image (juce::Image::ARGB, width, height, true);
    {
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }

    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (outName);
    if (auto stream = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        juce::PNGImageFormat png;
        if (! png.writeImageToStream (image, *stream))
        {
            std::printf ("FAIL: could not encode %s\n", outName.toRawUTF8());
            return 2;
        }
    }
    else
    {
        std::printf ("FAIL: could not open %s for writing\n", outName.toRawUTF8());
        return 2;
    }

    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), width, height);

    if (Assets::loadFailureCount() > 0)
    {
        std::printf ("\nWARNING: %d asset(s) failed to load:\n%s\n",
                     Assets::loadFailureCount(), Assets::describeFailures().toRawUTF8());
        return 1;
    }

    std::printf ("all artwork loaded from %s\n",
                 Assets::assetsDirectory().getFullPathName().toRawUTF8());
    return 0;
}
