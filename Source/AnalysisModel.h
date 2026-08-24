/*
    AnalysisModel.h - the data the UI renders.

    For the approved UI milestone every value is a placeholder that mirrors the
    approved mockup exactly, so the 50 %-opacity overlay QA compares like with
    like. The real analysis engine will fill the same structs later; nothing in
    the UI knows the difference.

    (README_START_HERE.md: "The reference mockup contains sample values and
    example file names. They are not hardcoded final product behavior.")
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
    juce::String fileName;
    juce::String tagA, tagB;
    int fitPercent = 0;
    bool favourite = false;
    juce::uint32 waveformSeed = 0;  // seeds the placeholder thumbnail
};

struct SourceStats
{
    juce::String peak, rms, crest, truePeak, duration, tempo, key;
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
    int score = 67;

    // Pods: Tone, Punch, Level, Phase, Fit - order fixed by the design.
    int tone = 63, punch = 72, level = 38, phase = 81, fit = 54;

    SourceStats stats { "-0.6 dBFS", "-10.7 dBFS", "10.1 dB",
                        "-0.3 dBTP", "1.82 s", "128.0 BPM", "C Minor" };

    std::vector<Diagnostic> diagnostics {
        { "Kick / 808 Masking Detected", "Strong frequency conflict in\n40-80 Hz region.",   Severity::high,   "alert_wave" },
        { "Headroom Too Hot",            "Peaks are constraining dynamics.\nReduce level or short transients.", Severity::medium, "gauge" },
        { "Low-End Body Weak",           "Insufficient energy in\n80-150 Hz range.",         Severity::medium, "wave" },
        { "Clipping Clean",              "No digital clipping detected.\nSignal integrity good.", Severity::good, "check" },
    };

    std::vector<RescueSuggestion> rescues {
        { "Kick_034.wav",   "Modern",  "Punchy",   96, false, 0x034u },
        { "Kick_021.wav",   "Deep",    "Tight",    92, false, 0x021u },
        { "Kick_047.wav",   "Warm",    "Full",     88, false, 0x047u },
        { "Sub_Kick_12.wav","Low",     "Extended", 85, false, 0x012u },
        { "Kick_009.wav",   "Vintage", "Thump",    82, false, 0x009u },
    };

    // Masking / fit view: five band scores (Sub, Low, Low Mid, High Mid, High)
    // and the radar polygons (0..1 per axis, source then mix target).
    int bandFit[5] { 58, 52, 47, 60, 63 };
    float radarSource[5] { 0.60f, 0.52f, 0.44f, 0.63f, 0.55f };
    float radarTarget[5] { 0.68f, 0.60f, 0.57f, 0.70f, 0.63f };

    // Spectrum conflict region (Hz); empty range disables the overlay.
    float conflictLoHz = 40.0f, conflictHiHz = 80.0f;
    juce::String conflictLabel = "KICK / 808 CONFLICT";
};

} // namespace sourceglo
