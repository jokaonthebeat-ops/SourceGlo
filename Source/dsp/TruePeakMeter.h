/*
    TruePeakMeter.h - inter-sample peak measurement in dBTP, ported from
    MasterGlo Pro. BS.1770-style 4x reconstruction; measurement only, never
    touches the audio path.
*/

#pragma once
#include "PolyphaseInterpolator.h"

namespace sourceglo
{

class TruePeakMeter
{
public:
    void reset() noexcept
    {
        left.reset();
        right.reset();
    }

    // Audio thread. Largest reconstructed absolute value in the block, linear.
    float processBlock (const float* l, const float* r, int numSamples) noexcept
    {
        float peak = 0.0f;
        for (int n = 0; n < numSamples; ++n)
            peak = juce::jmax (peak, left.peakForSample (l[n]), right.peakForSample (r[n]));
        return peak;
    }

private:
    PolyphasePeakDetector left, right;
};

} // namespace sourceglo
