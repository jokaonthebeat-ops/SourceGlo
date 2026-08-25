/*
    AnalysisEngine.h - the intelligence: measures a captured source and scores
    it against a per-source-type "modern pro standard" profile.

    Pure offline analysis: give it a buffer, get a result. Runs on a worker
    thread (or synchronously in the tests and the headless tools); it never
    touches the processor, allocates freely, and is deterministic - identical
    audio in, identical result out. tools/DspTest.cpp holds the ground-truth
    fixtures (known sines, click tracks, triads) that pin the maths down.
*/

#pragma once
#include <JuceHeader.h>
#include "../AnalysisModel.h"

namespace sourceglo
{

struct AnalysisResult
{
    bool enoughAudio = false;

    // Stats of the analysed capture.
    float peakDb = -120.0f, rmsDb = -120.0f, crestDb = 0.0f, truePeakDb = -120.0f;
    float durationSeconds = 0.0f;
    float tempoBpm = 0.0f;            // <= 0: no confident tempo
    juce::String keyName;             // empty: no confident key

    // Five display bands: Sub, Low, Low Mid, High Mid, High.
    static constexpr int numBands = 5;
    float bandLevelDb[numBands]   {};  // re loudest band (0 = loudest)
    float bandDeviationDb[numBands] {};// measured minus source-type target

    // Stereo.
    float correlation = 1.0f, lowCorrelation = 1.0f;
    bool dcOffset = false;

    // Scores 0..100.
    int score = 0, tone = 0, punch = 0, level = 0, phase = 0, fit = 0;
    int bandFit[numBands] {};
    float radarSource[numBands] {}, radarTarget[numBands] {};

    // Conflict overlay for the spectrum (0 width = none).
    float conflictLoHz = 0.0f, conflictHiHz = 0.0f;
    juce::String conflictLabel;

    std::vector<Diagnostic> diagnostics;
};

struct AnalysisEngine
{
    // sourceTypeIndex follows parameters.json: Auto, Kick, Snare, Clap, 808,
    // Bass, Hat, Percussion, Loop, Melody, Vocal, Other.
    static AnalysisResult analyse (const juce::AudioBuffer<float>& stereo,
                                   double sampleRate, int sourceTypeIndex);

    // The source-type target curve, for the spectrum's dashed reference
    // trace. Returns a display level in dB (0 dB = the curve's loudest
    // point); smooth in log-frequency.
    static float targetCurveDb (int sourceTypeIndex, float hz);

    // Band edges shared by the analyser and the UI.
    static const float* bandEdgesHz();   // 6 edges for 5 bands

    // The source-type profile's target level for one band (dB re loudest) -
    // the rescue matcher ranks library files against this.
    static float targetBandDb (int sourceTypeIndex, int band);
};

} // namespace sourceglo
