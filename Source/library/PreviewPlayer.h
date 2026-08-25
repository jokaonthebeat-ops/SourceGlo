/*
    PreviewPlayer.h - auditions a rescue sample through the plugin output.

    Loading happens on the message thread into whichever slot the audio
    thread is NOT reading; start() first parks playback and gives the audio
    thread a moment to let go before touching a buffer. The audio thread only
    ever advances a read position and mixes with a 5 ms fade - no allocation,
    no locks.
*/

#pragma once
#include <JuceHeader.h>

namespace sourceglo
{

class PreviewPlayer
{
public:
    void prepare (double sampleRate)
    {
        hostSr = sampleRate;
        fadePerSample = (float) (1.0 / (sampleRate * 0.005));
        activeSlot.store (-1);
        fadeGain = 0.0f;
    }

    // Message thread. Returns false if the file will not load.
    bool start (const juce::File& file, juce::AudioFormatManager& formats)
    {
        stop();
        juce::Thread::sleep (60);       // let the audio thread let go of the slots

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples < 64)
            return false;

        const int slot = juce::jmax (0, 1 - lastLoaded);
        auto& s = slots[slot];

        const int n = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                     (juce::int64) (reader->sampleRate * 15.0));
        s.buffer.setSize (2, n);
        juce::AudioBuffer<float> temp ((int) reader->numChannels, n);
        reader->read (&temp, 0, n, 0, true, true);
        for (int ch = 0; ch < 2; ++ch)
            s.buffer.copyFrom (ch, 0, temp, juce::jmin (ch, (int) reader->numChannels - 1), 0, n);

        s.ratio = reader->sampleRate / hostSr;
        s.path = file.getFullPathName();

        lastLoaded = slot;
        restartFlag.store (true);
        activeSlot.store (slot);
        return true;
    }

    void stop()                                  { activeSlot.store (-1); }

    juce::String getActivePath() const
    {
        const int slot = activeSlot.load();
        return slot >= 0 ? slots[slot].path : juce::String();
    }

    // Audio thread.
    void mixInto (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int slot = activeSlot.load (std::memory_order_acquire);

        if (slot != currentSlot || restartFlag.exchange (false))
        {
            currentSlot = slot;
            position = 0.0;
        }

        if (slot < 0)
        {
            if (fadeGain > 0.0f)                 // fade the tail of a stop
                fadeTail (buffer);
            return;
        }

        const auto& s = slots[slot];
        const int len = s.buffer.getNumSamples();
        const int numSamples = buffer.getNumSamples();
        const int numCh = juce::jmin (2, buffer.getNumChannels());

        for (int i = 0; i < numSamples; ++i)
        {
            if (position >= len - 1)
            {
                activeSlot.store (-1);           // finished: auto-stop
                return;
            }

            fadeGain = juce::jmin (1.0f, fadeGain + fadePerSample);
            const int i0 = (int) position;
            const float frac = (float) (position - i0);

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float a = s.buffer.getSample (ch, i0);
                const float b = s.buffer.getSample (ch, i0 + 1);
                buffer.addSample (ch, i, previewGain * fadeGain * (a + frac * (b - a)));
            }
            position += s.ratio;
        }
    }

private:
    void fadeTail (juce::AudioBuffer<float>& buffer) noexcept
    {
        // Nothing left to read from; just release the gain so the next start
        // fades in from silence.
        const int numSamples = buffer.getNumSamples();
        fadeGain = juce::jmax (0.0f, fadeGain - fadePerSample * (float) numSamples);
    }

    struct Slot
    {
        juce::AudioBuffer<float> buffer;
        double ratio = 1.0;
        juce::String path;
    };

    Slot slots[2];
    std::atomic<int> activeSlot { -1 };
    std::atomic<bool> restartFlag { false };
    int lastLoaded = 1, currentSlot = -1;
    double position = 0.0, hostSr = 48000.0;
    float fadeGain = 0.0f, fadePerSample = 0.01f;
    static constexpr float previewGain = 0.5f;   // -6 dB
};

} // namespace sourceglo
