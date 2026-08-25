/*
    AnalysisModel.h - the data the UI renders.

    Starts in the honest empty state: nothing analysed yet, scores unknown,
    no diagnostics. The processor fills it from AnalysisEngine results when
    the user runs Analyze. The rescue list stays placeholder until the
    library milestone.
*/

#pragma once
#include <JuceHeader.h>
#include "ui/Theme.h"

namespace sourceglo
{

enum class Severity { high, medium, good };

struct Diagnostic
{
    juce::String title;
    juce::String body;      // one or two lines
    Severity severity = Severity::good;
    juce::String icon;      // icon name in Assets/Icons
};

struct RescueSuggestion
{
    static constexpr int waveformBars = 24;

    juce::String path;              // absolute path in the user's library
    juce::String fileName;
    juce::String tagA, tagB;        // descriptors derived from the file's analysis
    int fitPercent = 0;
    bool favourite = false;
    std::array<float, waveformBars> waveform {};   // 0..1 overview bars
};

// Live/analysed source statistics. Peak/RMS/crest/true-peak update
// continuously from the processor's running meters; duration, tempo and key
// come from the last analysis.
struct SourceStats
{
    float peakDb = -120.0f, rmsDb = -120.0f, crestDb = 0.0f, truePeakDb = -120.0f;
    float durationSec = 0.0f;
    float tempoBpm = 0.0f;          // <= 0: unknown
    juce::String key;               // empty: unknown
};

// Score -> status phrase, thresholds from the master build prompt.
struct ScoreStatus
{
    static juce::String phrase (int score)
    {
        if (score >= 85) return "READY";
        if (score >= 70) return "GOOD";
        if (score >= 50) return "NEEDS WORK";
        return "FIX REQUIRED";
    }

    static juce::Colour colour (int score)
    {
        if (score >= 85) return tokens::green;
        if (score >= 70) return tokens::cyan;
        if (score >= 50) return tokens::gold;
        return tokens::red;
    }
};

struct AnalysisModel
{
    bool analyzed = false;

    int score = 0;
    int tone = 0, punch = 0, level = 0, phase = 0, fit = 0;

    SourceStats stats;

    std::vector<Diagnostic> diagnostics;   // empty until analysed

    // Filled by RescueLibrary::match; empty until the library has content.
    std::vector<RescueSuggestion> rescues;

    // Masking / fit view. Targets default to the mix-target pentagon so the
    // empty state still shows the goal shape.
    int bandFit[5] { 0, 0, 0, 0, 0 };
    float radarSource[5] { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float radarTarget[5] { 0.69f, 0.72f, 0.76f, 0.69f, 0.60f };

    // Spectrum conflict region (Hz); zero width disables the overlay.
    float conflictLoHz = 0.0f, conflictHiHz = 0.0f;
    juce::String conflictLabel;
};

} // namespace sourceglo
