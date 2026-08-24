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
    const juce::String modeArg = argc > 3 ? juce::String (argv[3]).toLowerCase() : "";

    int width = Design::width, height = Design::height;
    if (sizeArg == "min")      { width = Design::minWidth; height = Design::minHeight; }
    else if (sizeArg == "max") { width = Design::maxWidth; height = Design::maxHeight; }
    else if (sizeArg.containsChar ('x'))
    {
        width  = sizeArg.upToFirstOccurrenceOf ("x", false, false).getIntValue();
        height = sizeArg.fromFirstOccurrenceOf ("x", false, false).getIntValue();
    }

    SourceGloProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

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
        const int blocks = (int) (sr * 1.2 / blockSize);

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
    }

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
