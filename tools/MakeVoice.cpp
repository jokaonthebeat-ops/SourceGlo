/*
    MakeVoice - renders narration lines to WAV with AVSpeechSynthesizer.

    The `say` command line tool produces header-only files on this machine
    (4096 bytes, zero frames, with or without the sandbox), so the film's
    narration is synthesised through AVSpeechSynthesizer's offline
    writeUtterance API instead - which is the supported way to render speech
    without audio hardware, and lets the voice be chosen deterministically.

        makevoice <out.wav> <voice-substring|-> <text>
*/

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#import <AVFoundation/AVFoundation.h>
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    @autoreleasepool
    {
        if (argc < 4)
        {
            std::printf ("usage: makevoice <out.wav> <voice|-> <text>\n");
            return 1;
        }

        const juce::File outFile (juce::File::getCurrentWorkingDirectory()
                                    .getChildFile (juce::String (argv[1])));
        const juce::String voiceWanted (argv[2]);
        juce::String text;
        for (int i = 3; i < argc; ++i)
            text += juce::String (argv[i]) + (i + 1 < argc ? " " : "");

        auto* utterance = [AVSpeechUtterance speechUtteranceWithString:
                             [NSString stringWithUTF8String: text.toRawUTF8()]];

        // Prefer an enhanced/premium en-US voice: the compact ones sound
        // noticeably robotic under music.
        AVSpeechSynthesisVoice* chosen = nil;
        for (AVSpeechSynthesisVoice* v in [AVSpeechSynthesisVoice speechVoices])
        {
            const juce::String name (v.name.UTF8String);
            const juce::String ident (v.identifier.UTF8String);
            if (! ident.startsWith ("com.apple.voice") && ! ident.contains ("speech"))
                continue;
            if (! juce::String (v.language.UTF8String).startsWithIgnoreCase ("en-US"))
                continue;

            if (voiceWanted != "-")
            {
                if (name.containsIgnoreCase (voiceWanted))
                {
                    // An explicit request wins, but still prefer its
                    // enhanced variant when one exists.
                    if (chosen == nil || ident.contains ("premium") || ident.contains ("enhanced"))
                        chosen = v;
                }
                continue;
            }

            const bool better = ident.contains ("premium") || ident.contains ("enhanced");
            if (chosen == nil || (better && ! juce::String (chosen.identifier.UTF8String)
                                                .contains ("premium")))
                chosen = v;
        }

        if (chosen != nil)
            utterance.voice = chosen;
        utterance.rate = 0.48f;          // a touch under default: clearer over music
        utterance.pitchMultiplier = 1.0f;
        utterance.preUtteranceDelay = 0.0;

        std::printf ("voice: %s (%s)\n",
                     chosen != nil ? chosen.name.UTF8String : "system default",
                     chosen != nil ? chosen.identifier.UTF8String : "-");

        auto* synth = [[AVSpeechSynthesizer alloc] init];

        // The format object is reached through a pointer for the same reason
        // as the sink: a block captures locals by const value.
        juce::WavAudioFormat wav;
        juce::WavAudioFormat* wavPtr = &wav;
        outFile.deleteFile();
        // Blocks cannot assign to captured locals, so the writer and its
        // counters live in a small state object the block mutates.
        struct Sink
        {
            std::unique_ptr<juce::AudioFormatWriter> writer;
            double sampleRate = 0.0;
            juce::int64 frames = 0;
            int callbacks = 0;
            bool finished = false;
        };
        Sink sink;
        Sink* sinkPtr = &sink;

        [synth writeUtterance: utterance
                toBufferCallback: ^(AVAudioBuffer* buffer)
        {
            auto* pcm = (AVAudioPCMBuffer*) buffer;
            ++sinkPtr->callbacks;
            if (pcm == nil || pcm.frameLength == 0)
            {
                sinkPtr->finished = true;   // zero-length buffer ends the stream
                return;
            }

            const int channels = (int) pcm.format.channelCount;
            const int frames = (int) pcm.frameLength;

            if (sinkPtr->writer == nullptr)
            {
                sinkPtr->sampleRate = pcm.format.sampleRate;
                if (auto stream = std::unique_ptr<juce::OutputStream> (outFile.createOutputStream()))
                {
                    const auto options = juce::AudioFormatWriterOptions{}
                                           .withSampleRate (sinkPtr->sampleRate)
                                           .withNumChannels (1)
                                           .withBitsPerSample (16);
                    sinkPtr->writer = wavPtr->createWriterFor (stream, options);
                }
            }

            if (sinkPtr->writer == nullptr)
                return;

            juce::AudioBuffer<float> mono (1, frames);
            if (pcm.floatChannelData != nullptr)
            {
                mono.copyFrom (0, 0, pcm.floatChannelData[0], frames);
                for (int ch = 1; ch < channels; ++ch)
                    mono.addFrom (0, 0, pcm.floatChannelData[ch], frames);
                if (channels > 1)
                    mono.applyGain (1.0f / (float) channels);
            }
            else if (pcm.int16ChannelData != nullptr)
            {
                auto* src = pcm.int16ChannelData[0];
                for (int i = 0; i < frames; ++i)
                    mono.setSample (0, i, (float) src[i * channels] / 32768.0f);
            }

            sinkPtr->writer->writeFromAudioSampleBuffer (mono, 0, frames);
            sinkPtr->frames += frames;
        }];

        // writeUtterance delivers its buffers on an internal queue; pump the
        // run loop until the terminating empty buffer arrives.
        const double deadline = juce::Time::getMillisecondCounterHiRes() + 15000.0;
        while (! sink.finished && juce::Time::getMillisecondCounterHiRes() < deadline)
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false);

        sink.writer.reset();

        if (sink.frames == 0)
        {
            std::printf ("no audio produced (callbacks=%d, finished=%d)\n",
                         sink.callbacks, (int) sink.finished);
            return 1;
        }

        std::printf ("wrote %s  %.2f s @ %.0f Hz\n",
                     outFile.getFullPathName().toRawUTF8(),
                     (double) sink.frames / juce::jmax (1.0, sink.sampleRate),
                     sink.sampleRate);
    }
    return 0;
}
