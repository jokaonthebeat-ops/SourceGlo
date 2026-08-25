/*
    FixChain.h - the correction and macro processing chain.

    Order: fix trim -> corrective EQ (from the analysis) -> macro EQ
    (Body / Tone / Air) -> transient shaping (Punch / Transients) ->
    saturation (oversampled per the Oversampling parameter) -> low-mono fix
    and Stereo width -> chain wet/dry (the A/B compare).

    The fix targets are computed on the message thread from the last
    AnalysisResult (engageFix) and handed to the audio thread through
    atomics; every gain moves through a smoother, so engaging or scaling the
    fix never clicks. tools/DspTest.cpp measures each stage against absolute
    targets (band gains, harmonic content, width ratios, reported latency).
*/

#pragma once
#include <JuceHeader.h>
#include "AnalysisEngine.h"

namespace sourceglo
{

class FixChain
{
public:
    FixChain() = default;      // the deleted copy ctor suppresses the implicit one

    static constexpr int numFixBands = AnalysisResult::numBands;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    struct MacroValues        // 0..1 each, read from the APVTS per block
    {
        float punch = 0.0f, body = 0.0f, tone = 0.5f, air = 0.0f;
        float stereo = 0.0f, transients = 0.0f, saturate = 0.0f;
        float fixAmount = 0.5f;
        int oversampling = 2;     // 0 Off, 1 2x, 2 4x, 3 8x
        bool hq = true;
        bool compare = false;     // A/B: true = raw side (chain dry)
    };

    void process (juce::AudioBuffer<float>& buffer, const MacroValues& macros) noexcept;

    // Message thread: derive the correction from an analysis. fixAmount and
    // engagement are applied on the audio thread.
    void engageFix (const AnalysisResult& analysis);
    void disengageFix();
    bool isFixEngaged() const noexcept          { return fixEngaged.load(); }

    // Raw correction values, for persistence (state save/load).
    struct FixState
    {
        float bandGainDb[numFixBands] {};
        float trimDb = 0.0f;
        bool  lowMono = false;
        bool  dcFilter = false;
    };
    FixState getFixState() const;
    void setFixState (const FixState& state, bool engaged);

    // Latency of the currently selected oversampler, in samples.
    int getLatencySamples (int oversamplingIndex, bool hq) const noexcept;

private:
    void updateFilters (float fixAmount, const MacroValues& macros) noexcept;
    float transientGain (float sampleL, float sampleR, float punchAmt, float transAmt) noexcept;

    double sr = 48000.0;

    // --- fix targets (message thread writes, audio thread reads) ----------
    std::atomic<float> fixBandDb[numFixBands] { {0}, {0}, {0}, {0}, {0} };
    std::atomic<float> fixTrimDb { 0.0f };
    std::atomic<bool>  fixLowMono { false };
    std::atomic<bool>  fixDc { false };
    std::atomic<bool>  fixEngaged { false };

    // --- smoothers ---------------------------------------------------------
    juce::SmoothedValue<float> fixBandSm[numFixBands], fixTrimSm;
    juce::SmoothedValue<float> bodySm, toneSm, airSm, widthSm, chainMixSm, lowMonoSm;

    // --- filters: [channel] ------------------------------------------------
    // 5 corrective bands + body bell + tone tilt (low & high shelf) + air.
    static constexpr int numFilters = numFixBands + 4;
    juce::dsp::IIR::Filter<float> filters[2][numFilters];
    float lastFilterGain[numFilters] { 1e9f, 1e9f, 1e9f, 1e9f, 1e9f,
                                       1e9f, 1e9f, 1e9f, 1e9f };
    juce::dsp::IIR::Filter<float> dcFilters[2], sideHighPass;

    // --- transient shaper --------------------------------------------------
    float envFast = 0.0f, envSlow = 0.0f, envLowFast = 0.0f, envLowSlow = 0.0f;
    float lowLpState = 0.0f;
    float fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
    float lowLpCoeff = 0.0f;

    // --- saturation + oversampling ----------------------------------------
    std::unique_ptr<juce::dsp::Oversampling<float>> oversamplers[3];   // 2x 4x 8x
    int preparedBlockSize = 0;

    juce::AudioBuffer<float> dryChain;    // pre-chain copy for A/B mix

    JUCE_DECLARE_NON_COPYABLE (FixChain)
};

} // namespace sourceglo
