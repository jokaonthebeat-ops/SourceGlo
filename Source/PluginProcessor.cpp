#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace sourceglo
{

// -----------------------------------------------------------------------------
//  Parameter layout - Spec/.../08_LAYOUT/parameters.json, verbatim.
//  tools/DspTest.cpp reads the JSON back at runtime and asserts this table
//  matches it, so a drift between spec and code is a failing test.
// -----------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout SourceGloProcessor::createLayout()
{
    using P  = juce::AudioParameterFloat;
    using B  = juce::AudioParameterBool;
    using C  = juce::AudioParameterChoice;
    namespace ids = pid;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<P> (juce::ParameterID { id, 1 }, name,
                 juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
                 juce::AudioParameterFloatAttributes().withLabel ("%"));
    };

    layout.add (std::make_unique<C> (juce::ParameterID { ids::sourceType, 1 }, "Source Type",
                  juce::StringArray { "Auto", "Kick", "Snare", "Clap", "808", "Bass",
                                      "Hat", "Percussion", "Loop", "Melody", "Vocal", "Other" }, 0));

    layout.add (std::make_unique<P> (juce::ParameterID { ids::inputGain, 1 }, "Input Gain",
                  juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                  juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<P> (juce::ParameterID { ids::outputGain, 1 }, "Output Gain",
                  juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                  juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<B> (juce::ParameterID { ids::phaseInvert, 1 }, "Phase Invert", false));
    layout.add (std::make_unique<B> (juce::ParameterID { ids::mono, 1 }, "Mono", false));

    layout.add (pct (ids::fixAmount,  "Fix Amount", 50.0f));
    layout.add (pct (ids::punch,      "Punch",      60.0f));
    layout.add (pct (ids::body,       "Body",       55.0f));
    layout.add (pct (ids::tone,       "Tone",       50.0f));
    layout.add (pct (ids::air,        "Air",        40.0f));
    layout.add (pct (ids::stereo,     "Stereo",     20.0f));
    layout.add (pct (ids::transients, "Transients", 65.0f));
    layout.add (pct (ids::saturate,   "Saturate",   35.0f));

    layout.add (std::make_unique<B> (juce::ParameterID { ids::autoMatch, 1 }, "Auto Match", true));
    layout.add (std::make_unique<B> (juce::ParameterID { ids::hq, 1 }, "HQ", true));

    layout.add (std::make_unique<C> (juce::ParameterID { ids::oversampling, 1 }, "Oversampling",
                  juce::StringArray { "Off", "2x", "4x", "8x" }, 2));

    layout.add (std::make_unique<B> (juce::ParameterID { ids::bypass, 1 }, "Bypass", false));

    return layout;
}

SourceGloProcessor::SourceGloProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SourceGloPro", createLayout())
{
    fftBuffer.resize ((size_t) fftFifo.getTotalSize());
}

bool SourceGloProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SourceGloProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
    inGainSm.reset (sampleRate, 0.02);
    outGainSm.reset (sampleRate, 0.02);
    bypassMix.reset (sampleRate, 0.05);        // click-free power button

    fftFifo.reset();
}

void SourceGloProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh      = juce::jmin (2, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    const bool phaseInv = apvts.getRawParameterValue (pid::phaseInvert)->load() > 0.5f;
    const bool monoSum  = apvts.getRawParameterValue (pid::mono)->load() > 0.5f;
    const bool bypassed = apvts.getRawParameterValue (pid::bypass)->load() > 0.5f;

    inGainSm.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (pid::inputGain)->load()));
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (pid::outputGain)->load()));
    bypassMix.setTargetValue (bypassed ? 0.0f : 1.0f);

    // Keep an untouched copy so bypass can crossfade to true dry.
    juce::AudioBuffer<float> dry;
    dry.makeCopyOf (buffer, true);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inGainSm.getNextValue() * (phaseInv ? -1.0f : 1.0f);
        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    if (monoSum && numCh == 2)
    {
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            l[i] = r[i] = m;
        }
    }

    // Input meters + spectrum feed, post-trim (what the display calls INPUT).
    for (int ch = 0; ch < numCh; ++ch)
    {
        inPeak[ch].store (buffer.getMagnitude (ch, 0, numSamples));
        inRms[ch].store (buffer.getRMSLevel (ch, 0, numSamples));
    }

    {
        int start1, size1, start2, size2;
        fftFifo.prepareToWrite (numSamples, start1, size1, start2, size2);
        const float* l = buffer.getReadPointer (0);
        const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;

        for (int i = 0; i < size1; ++i)
            fftBuffer[(size_t) (start1 + i)] = 0.5f * (l[i] + r[i]);
        for (int i = 0; i < size2; ++i)
            fftBuffer[(size_t) (start2 + i)] = 0.5f * (l[size1 + i] + r[size1 + i]);

        fftFifo.finishedWrite (size1 + size2);
    }

    // (Correction/macro DSP lands after the visual milestone.)

    for (int i = 0; i < numSamples; ++i)
    {
        const float g   = outGainSm.getNextValue();
        const float wet = bypassMix.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            d[i] = d[i] * g * wet + dry.getReadPointer (ch)[i] * (1.0f - wet);
        }
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        outPeak[ch].store (buffer.getMagnitude (ch, 0, numSamples));
        outRms[ch].store (buffer.getRMSLevel (ch, 0, numSamples));
    }
}

bool SourceGloProcessor::pullFFTBlock (float* dest)
{
    if (fftFifo.getNumReady() < fftSize)
        return false;

    int start1, size1, start2, size2;
    fftFifo.prepareToRead (fftSize, start1, size1, start2, size2);
    std::memcpy (dest, fftBuffer.data() + start1, sizeof (float) * (size_t) size1);
    if (size2 > 0)
        std::memcpy (dest + size1, fftBuffer.data() + start2, sizeof (float) * (size_t) size2);
    fftFifo.finishedRead (size1 + size2);
    return true;
}

// -----------------------------------------------------------------------------
//  Commands (UI milestone: placeholder behaviour, real engine later)
// -----------------------------------------------------------------------------
void SourceGloProcessor::requestAnalyze()
{
    // Placeholder analysis pass: flags the busy state, then publishes the
    // model. Runs on the message thread via a timer-free async call.
    analyzing.store (true);
    juce::MessageManager::callAsync ([safe = juce::WeakReference<SourceGloProcessor> (this)]
    {
        if (auto* p = safe.get())
        {
            p->analyzing.store (false);
            p->analysisChanged.sendChangeMessage();
        }
    });
}

void SourceGloProcessor::requestFixSource()
{
    // Real fix engine lands with the DSP milestone.
    analysisChanged.sendChangeMessage();
}

// -----------------------------------------------------------------------------
//  Presets (placeholder list so the header responds; real bank comes later)
// -----------------------------------------------------------------------------
juce::StringArray SourceGloProcessor::getPresetNames() const
{
    return { "Punchy Kick Starter", "Deep 808 Control", "Snare Snap Doctor",
             "Bass Focus Clean", "Vocal Clarity Rescue", "Loop Glue Fast" };
}

void SourceGloProcessor::selectPreset (int index)
{
    const auto names = getPresetNames();
    presetIndex = (index % names.size() + names.size()) % names.size();
}

juce::String SourceGloProcessor::getPresetName() const
{
    return getPresetNames()[presetIndex];
}

// -----------------------------------------------------------------------------
//  State
// -----------------------------------------------------------------------------
void SourceGloProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetIndex", presetIndex, nullptr);
    state.setProperty ("uiScale", uiScale.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SourceGloProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            presetIndex = (int) state.getProperty ("presetIndex", 0);
            uiScale.store ((float) (double) state.getProperty ("uiScale", 1.0));
            apvts.replaceState (state);
        }
    }
}

juce::AudioProcessorEditor* SourceGloProcessor::createEditor()
{
    return new SourceGloEditor (*this);
}

} // namespace sourceglo

// JUCE plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sourceglo::SourceGloProcessor();
}
