/*
    SourceGlo Pro - processor.

    UI-milestone scope: the full parameter contract from
    Spec/.../08_LAYOUT/parameters.json, honest gain/phase/mono/bypass audio,
    meter taps and an FFT feed for the spectrum display. The intelligence
    (analysis, scoring, fix engine, rescue matching) lands after the visual
    milestone is approved - the UI reads everything through AnalysisModel, so
    the engine can be swapped in without touching the interface.
*/

#pragma once
#include <JuceHeader.h>
#include "AnalysisModel.h"

namespace sourceglo
{

namespace pid
{
    inline constexpr const char* sourceType   = "sourceType";
    inline constexpr const char* inputGain    = "inputGain";
    inline constexpr const char* outputGain   = "outputGain";
    inline constexpr const char* phaseInvert  = "phaseInvert";
    inline constexpr const char* mono         = "mono";
    inline constexpr const char* fixAmount    = "fixAmount";
    inline constexpr const char* punch        = "punch";
    inline constexpr const char* body         = "body";
    inline constexpr const char* tone         = "tone";
    inline constexpr const char* air          = "air";
    inline constexpr const char* stereo       = "stereo";
    inline constexpr const char* transients   = "transients";
    inline constexpr const char* saturate     = "saturate";
    inline constexpr const char* autoMatch    = "autoMatch";
    inline constexpr const char* hq           = "hq";
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* bypass       = "bypass";
}

class SourceGloProcessor : public juce::AudioProcessor
{
public:
    SourceGloProcessor();
    ~SourceGloProcessor() override = default;

    // --- AudioProcessor --------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return JucePlugin_Name; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- state -----------------------------------------------------------
    juce::AudioProcessorValueTreeState& getAPVTS()         { return apvts; }
    juce::UndoManager& getUndoManager()                    { return undoManager; }

    // Preset display state (placeholder browser for the UI milestone).
    juce::StringArray getPresetNames() const;
    int getPresetIndex() const                             { return presetIndex; }
    void selectPreset (int index);
    juce::String getPresetName() const;

    // UI scale persisted with the session.
    float getSavedUIScale() const                          { return uiScale.load(); }
    void  setSavedUIScale (float s)                        { uiScale.store (s); }

    // --- analysis --------------------------------------------------------
    AnalysisModel& getAnalysis()                           { return analysis; }
    juce::ChangeBroadcaster analysisChanged;

    // Command-style actions (master prompt: callbacks, not parameters).
    void requestAnalyze();
    void requestFixSource();
    bool isAnalyzing() const                               { return analyzing.load(); }

    // --- meters + spectrum feed ------------------------------------------
    // Block peak / rms per channel, written on the audio thread, read by the
    // UI which applies its own ballistics.
    std::atomic<float> inPeak[2]  { { 0.0f }, { 0.0f } };
    std::atomic<float> inRms[2]   { { 0.0f }, { 0.0f } };
    std::atomic<float> outPeak[2] { { 0.0f }, { 0.0f } };
    std::atomic<float> outRms[2]  { { 0.0f }, { 0.0f } };

    // Mono FFT feed: audio thread writes, UI thread reads. Lock-free.
    static constexpr int fftOrder = 11;                    // 2048
    static constexpr int fftSize  = 1 << fftOrder;
    bool pullFFTBlock (float* dest);                       // fftSize samples; false if starved
    double getSampleRateHz() const                         { return currentSampleRate.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::UndoManager undoManager;                         // NOT given to the APVTS
    juce::AudioProcessorValueTreeState apvts;

    juce::SmoothedValue<float> inGainSm, outGainSm, bypassMix;
    std::atomic<double> currentSampleRate { 48000.0 };

    juce::AbstractFifo fftFifo { fftSize * 4 };
    std::vector<float> fftBuffer;

    AnalysisModel analysis;
    std::atomic<bool> analyzing { false };

    int presetIndex = 0;
    std::atomic<float> uiScale { 1.0f };

    JUCE_DECLARE_WEAK_REFERENCEABLE (SourceGloProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SourceGloProcessor)
};

} // namespace sourceglo
