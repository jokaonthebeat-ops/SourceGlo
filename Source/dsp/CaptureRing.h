/*
    CaptureRing.h - rolling stereo capture of the source.

    The audio thread writes every processed block into a fixed 12-second ring;
    the message thread snapshots the most recent audio when the user hits
    Analyze. Lock-free single-writer / single-reader: the writer only advances
    an atomic sample counter, the reader only copies. A snapshot can tear by
    at most one audio block at the write boundary, which is irrelevant to a
    multi-second statistical analysis - and worth it to keep processBlock()
    free of locks (JUCE_IMPLEMENTATION_SPEC section 5).
*/

#pragma once
#include <JuceHeader.h>

namespace sourceglo
{

class CaptureRing
{
public:
    static constexpr double seconds = 12.0;

    void prepare (double sampleRate)
    {
        capacity = juce::jmax (1, (int) std::llround (sampleRate * seconds));
        left.assign ((size_t) capacity, 0.0f);
        right.assign ((size_t) capacity, 0.0f);
        written.store (0);
        sr = sampleRate;
    }

    double sampleRate() const noexcept   { return sr; }

    // Audio thread.
    void push (const float* l, const float* r, int numSamples) noexcept
    {
        if (capacity == 0)
            return;

        auto pos = written.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto idx = (size_t) ((pos + i) % capacity);
            left[idx]  = l[i];
            right[idx] = r[i];
        }
        written.store (pos + numSamples, std::memory_order_release);
    }

    // Message thread. Copies the most recent audio (up to the ring length)
    // into dest; returns the number of samples copied.
    int snapshot (juce::AudioBuffer<float>& dest) const
    {
        const auto total = written.load (std::memory_order_acquire);
        const int  avail = (int) juce::jmin ((juce::int64) capacity, total);
        if (avail <= 0)
        {
            dest.setSize (2, 0);
            return 0;
        }

        dest.setSize (2, avail);
        const auto start = total - avail;
        for (int i = 0; i < avail; ++i)
        {
            const auto idx = (size_t) ((start + i) % capacity);
            dest.setSample (0, i, left[idx]);
            dest.setSample (1, i, right[idx]);
        }
        return avail;
    }

private:
    std::vector<float> left, right;
    int capacity = 0;
    double sr = 48000.0;
    std::atomic<juce::int64> written { 0 };
};

} // namespace sourceglo
