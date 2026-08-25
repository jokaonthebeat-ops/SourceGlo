/*
    PolyphaseInterpolator.h - shared 4x reconstruction used for true peak.

    Both the output true-peak meter and  need the same
    thing: the largest value the signal reaches *between* samples. They share
    this 48 tap Kaiser-windowed sinc, split into four 12 tap phases, rather
    than each carrying its own copy - two slightly different reconstructions
    would let the meter and the limiter disagree about the same peak, which is
    the sort of discrepancy a customer reports as "it goes over the ceiling".

    Each phase is normalised to unit DC gain, so a full-scale sustained signal
    reads exactly 0 dBTP.

    Coefficients are designed once into a shared static table. Nothing here
    allocates; peakForSample() is a fixed 48 multiply-accumulates.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace sourceglo
{

struct PolyphaseKernel
{
    static constexpr int phases = 4;
    static constexpr int tapsPerPhase = 12;
    static constexpr int totalTaps = phases * tapsPerPhase;

    std::array<std::array<float, tapsPerPhase>, phases> coeffs {};

    static const PolyphaseKernel& get()
    {
        static const PolyphaseKernel kernel = design();
        return kernel;
    }

private:
    static double sinc (double x) noexcept
    {
        if (std::abs (x) < 1.0e-9)
            return 1.0;
        const double px = juce::MathConstants<double>::pi * x;
        return std::sin (px) / px;
    }

    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x * 0.5) / (double) k;
            const double contribution = term * term;
            sum += contribution;
            if (contribution < 1.0e-16 * sum)
                break;
        }
        return sum;
    }

    static PolyphaseKernel design()
    {
        constexpr double beta = 8.0;      // ~80 dB stopband
        constexpr double cutoff = 0.94;   // small guard band below Nyquist
        const double centre = (totalTaps - 1) * 0.5;
        const double i0Beta = besselI0 (beta);

        std::array<double, totalTaps> proto {};
        for (int n = 0; n < totalTaps; ++n)
        {
            const double x = ((double) n - centre) / (double) phases;
            const double w = (double) n / (double) (totalTaps - 1) * 2.0 - 1.0;
            proto[(size_t) n] = sinc (cutoff * x)
                                  * besselI0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - w * w)))
                                  / i0Beta;
        }

        PolyphaseKernel k;
        for (int p = 0; p < phases; ++p)
        {
            double sum = 0.0;
            for (int t = 0; t < tapsPerPhase; ++t)
                sum += proto[(size_t) (t * phases + p)];

            const double norm = std::abs (sum) > 1.0e-12 ? 1.0 / sum : 1.0;
            for (int t = 0; t < tapsPerPhase; ++t)
                k.coeffs[(size_t) p][(size_t) t] = (float) (proto[(size_t) (t * phases + p)] * norm);
        }
        return k;
    }
};

/*
    One channel of running 4x peak reconstruction.
*/
class PolyphasePeakDetector
{
public:
    PolyphasePeakDetector() : kernel (PolyphaseKernel::get()) {}

    void reset() noexcept
    {
        history.fill (0.0f);
        writePos = 0;
    }

    // Feeds one sample and returns the largest reconstructed magnitude around
    // it. The reconstruction is inherently delayed by half the kernel; for a
    // peak *estimate* that offset does not matter, because the limiter and the
    // meter both consume the same delayed view.
    inline float peakForSample (float x) noexcept
    {
        history[(size_t) writePos] = x;
        writePos = (writePos + 1) % PolyphaseKernel::tapsPerPhase;

        float peak = std::abs (x);

        for (int p = 0; p < PolyphaseKernel::phases; ++p)
        {
            float acc = 0.0f;
            const auto* c = kernel.coeffs[(size_t) p].data();

            for (int t = 0; t < PolyphaseKernel::tapsPerPhase; ++t)
            {
                // writePos now points at the oldest slot, so walking forward
                // from it reads the history oldest-first.
                const int idx = (writePos + t) % PolyphaseKernel::tapsPerPhase;
                acc += c[t] * history[(size_t) idx];
            }

            peak = juce::jmax (peak, std::abs (acc));
        }

        return peak;
    }

private:
    const PolyphaseKernel& kernel;
    std::array<float, PolyphaseKernel::tapsPerPhase> history {};
    int writePos = 0;
};

} // namespace sourceglo
