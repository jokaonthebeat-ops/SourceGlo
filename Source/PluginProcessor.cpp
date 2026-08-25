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
    postBuffer.resize ((size_t) postFifo.getTotalSize());
    previewFormats.registerBasicFormats();
    library.changed.addChangeListener (this);

    presetBank = std::make_unique<PresetBank> (apvts, undoManager);

    // A previously indexed library fills the suggestions straight away.
    if (library.getIndexedCount() > 0)
        analysis.rescues = library.match (
            (int) apvts.getRawParameterValue (pid::sourceType)->load());

    // The pods follow the knobs: any creative parameter marks the analysis
    // dirty and the debounced re-analyzer refreshes it.
    for (const char* id : { pid::sourceType, pid::fixAmount, pid::punch, pid::body,
                            pid::tone, pid::air, pid::stereo, pid::transients,
                            pid::saturate })
        apvts.addParameterListener (id, this);
    reanalyzer = std::make_unique<Reanalyzer> (*this);
}

SourceGloProcessor::~SourceGloProcessor()
{
    reanalyzer.reset();
    for (const char* id : { pid::sourceType, pid::fixAmount, pid::punch, pid::body,
                            pid::tone, pid::air, pid::stereo, pid::transients,
                            pid::saturate })
        apvts.removeParameterListener (id, this);
    library.changed.removeChangeListener (this);
    // A worker job captures a WeakReference, but the pool itself must not
    // outlive the object whose member it is.
    analysisPool.removeAllJobs (true, 2000);
}

bool SourceGloProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SourceGloProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);
    inGainSm.reset (sampleRate, 0.02);
    outGainSm.reset (sampleRate, 0.02);
    bypassMix.reset (sampleRate, 0.05);        // click-free power button

    fftFifo.reset();
    postFifo.reset();
    capture.prepare (sampleRate);
    liveTruePeak.reset();

    fixChain.prepare (sampleRate, samplesPerBlock);
    previewPlayer.prepare (sampleRate);
    reportedLatency = -1;   // re-report on the first block
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

    // Rolling capture + live true peak of the source (post-trim).
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;
        capture.push (l, r, numSamples);

        const float tp = liveTruePeak.processBlock (l, r, numSamples);
        float expected = truePeakLinear.load (std::memory_order_relaxed);
        while (tp > expected
                && ! truePeakLinear.compare_exchange_weak (expected, tp,
                                                           std::memory_order_relaxed))
            {}
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

    // --- correction + macro chain ------------------------------------------
    {
        FixChain::MacroValues macros;
        auto pct = [this] (const char* id) {
            return apvts.getRawParameterValue (id)->load() * 0.01f; };
        macros.punch      = pct (pid::punch);
        macros.body       = pct (pid::body);
        macros.tone       = pct (pid::tone);
        macros.air        = pct (pid::air);
        macros.stereo     = pct (pid::stereo);
        macros.transients = pct (pid::transients);
        macros.saturate   = pct (pid::saturate);
        macros.fixAmount  = pct (pid::fixAmount);
        macros.oversampling = (int) apvts.getRawParameterValue (pid::oversampling)->load();
        macros.hq         = apvts.getRawParameterValue (pid::hq)->load() > 0.5f;
        macros.compare    = compareRaw.load();

        fixChain.process (buffer, macros);

        // Report oversampling latency when the setting changes. Doing it here
        // keeps it in lockstep with the block that actually changed.
        const int latency = fixChain.getLatencySamples (macros.oversampling, macros.hq);
        if (latency != reportedLatency)
        {
            reportedLatency = latency;
            setLatencySamples (latency);
        }
    }

    // Post-chain spectrum feed (the display's "Post" view).
    {
        int start1, size1, start2, size2;
        postFifo.prepareToWrite (numSamples, start1, size1, start2, size2);
        const float* l = buffer.getReadPointer (0);
        const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;

        for (int i = 0; i < size1; ++i)
            postBuffer[(size_t) (start1 + i)] = 0.5f * (l[i] + r[i]);
        for (int i = 0; i < size2; ++i)
            postBuffer[(size_t) (start2 + i)] = 0.5f * (l[size1 + i] + r[size1 + i]);

        postFifo.finishedWrite (size1 + size2);
    }

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

    // Rescue preview audition rides on top of the output.
    previewPlayer.mixInto (buffer);

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

bool SourceGloProcessor::pullPostFFTBlock (float* dest)
{
    if (postFifo.getNumReady() < fftSize)
        return false;

    int start1, size1, start2, size2;
    postFifo.prepareToRead (fftSize, start1, size1, start2, size2);
    std::memcpy (dest, postBuffer.data() + start1, sizeof (float) * (size_t) size1);
    if (size2 > 0)
        std::memcpy (dest + size1, postBuffer.data() + start2, sizeof (float) * (size_t) size2);
    postFifo.finishedRead (size1 + size2);
    return true;
}

// -----------------------------------------------------------------------------
//  Commands
// -----------------------------------------------------------------------------
void SourceGloProcessor::publishResult (const AnalysisResult& result)
{
    // Message thread only.
    analysis.analyzed = result.enoughAudio;

    if (result.enoughAudio)
    {
        analysis.score = result.score;
        analysis.tone  = result.tone;
        analysis.punch = result.punch;
        analysis.level = result.level;
        analysis.phase = result.phase;
        analysis.fit   = result.fit;

        analysis.stats.durationSec = result.durationSeconds;
        analysis.stats.tempoBpm    = result.tempoBpm;
        analysis.stats.key         = result.keyName;

        for (int b = 0; b < AnalysisResult::numBands; ++b)
        {
            analysis.bandFit[b]         = result.bandFit[b];
            analysis.radarSource[b]     = result.radarSource[b];
            analysis.radarTarget[b]     = result.radarTarget[b];
            analysis.bandLevelDb[b]     = result.bandLevelDb[b];
            analysis.bandDeviationDb[b] = result.bandDeviationDb[b];
        }
        analysis.correlation    = result.correlation;
        analysis.lowCorrelation = result.lowCorrelation;

        analysis.conflictLoHz  = result.conflictLoHz;
        analysis.conflictHiHz  = result.conflictHiHz;
        analysis.conflictLabel = result.conflictLabel;
    }

    analysis.diagnostics = result.diagnostics;
    lastAnalysis = result;

    if (apvts.getRawParameterValue (pid::autoMatch)->load() > 0.5f)
        analysis.rescues = library.match (
            (int) apvts.getRawParameterValue (pid::sourceType)->load());

    analyzing.store (false);
    analysisChanged.sendChangeMessage();
}

// Renders the captured raw source through a private copy of the correction
// chain under the CURRENT settings, so analysis measures what would actually
// leave the plugin. A local chain keeps the audio-thread chain's filter and
// envelope state untouched, and works with the transport stopped.
void SourceGloProcessor::renderThroughChain (juce::AudioBuffer<float>& buffer,
                                             double sampleRate)
{
    if (buffer.getNumSamples() == 0)
        return;

    constexpr int blockSize = 512;
    FixChain offline;
    offline.prepare (sampleRate, blockSize);
    offline.setFixState (fixChain.getFixState(), fixChain.isFixEngaged());

    FixChain::MacroValues macros;
    auto pct = [this] (const char* id)
    {
        return apvts.getRawParameterValue (id)->load() * 0.01f;
    };
    macros.punch      = pct (pid::punch);
    macros.body       = pct (pid::body);
    macros.tone       = pct (pid::tone);
    macros.air        = pct (pid::air);
    macros.stereo     = pct (pid::stereo);
    macros.transients = pct (pid::transients);
    macros.saturate   = pct (pid::saturate);
    macros.fixAmount  = pct (pid::fixAmount);
    macros.oversampling = (int) apvts.getRawParameterValue (pid::oversampling)->load();
    macros.hq         = apvts.getRawParameterValue (pid::hq)->load() > 0.5f;
    macros.compare    = false;      // A/B is an audition tool, not a setting

    // Let the chain's smoothers glide to their targets on silence first, so
    // the start of the render is not measured under half-applied settings.
    juce::AudioBuffer<float> warmup (2, blockSize);
    for (int i = 0; i < 80; ++i)    // ~0.85 s at 48 kHz
    {
        warmup.clear();
        offline.process (warmup, macros);
    }

    juce::AudioBuffer<float> block (2, blockSize);
    for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
    {
        const int n = juce::jmin (blockSize, buffer.getNumSamples() - start);
        block.clear();
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom (ch, 0, buffer, juce::jmin (ch, buffer.getNumChannels() - 1),
                            start, n);
        offline.process (block, macros);
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
            buffer.copyFrom (ch, start, block, ch, 0, n);
    }
}

juce::AudioBuffer<float> SourceGloProcessor::makeProcessedSnapshot()
{
    juce::AudioBuffer<float> snapshot;
    capture.snapshot (snapshot);
    renderThroughChain (snapshot, capture.sampleRate());
    return snapshot;
}

void SourceGloProcessor::requestAnalyze()
{
    if (analyzing.exchange (true))
        return;                                    // one pass at a time

    // Snapshot + offline chain render on the calling (message) thread, crunch
    // on the pool, publish back on the message thread. The WeakReference
    // guards against the processor being destroyed while the job runs.
    auto snapshot = std::make_shared<juce::AudioBuffer<float>> (makeProcessedSnapshot());
    const double sr = capture.sampleRate();
    const int type = (int) apvts.getRawParameterValue (pid::sourceType)->load();

    analysisPool.addJob ([safe = juce::WeakReference<SourceGloProcessor> (this),
                          snapshot, sr, type]
    {
        const auto result = AnalysisEngine::analyse (*snapshot, sr, type);
        juce::MessageManager::callAsync ([safe, result]
        {
            if (auto* p = safe.get())
                p->publishResult (result);
        });
    });
}

void SourceGloProcessor::analyzeNow()
{
    auto snapshot = makeProcessedSnapshot();
    const int type = (int) apvts.getRawParameterValue (pid::sourceType)->load();
    publishResult (AnalysisEngine::analyse (snapshot, capture.sampleRate(), type));
}

// -----------------------------------------------------------------------------
//  Auto re-analysis: the pods follow the knobs.
// -----------------------------------------------------------------------------
void SourceGloProcessor::parameterChanged (const juce::String&, float)
{
    // Can fire on any thread (automation): just stamp the time; the
    // message-thread timer does the work after a debounce.
    dirtyAtMs.store (juce::Time::getMillisecondCounter());
}

void SourceGloProcessor::Reanalyzer::timerCallback()
{
    const auto dirty = owner.dirtyAtMs.load();
    if (dirty == 0 || ! owner.analysis.analyzed || owner.analyzing.load())
        return;

    const auto now = juce::Time::getMillisecondCounter();
    if (now - dirty < 400)          // let a knob drag settle
        return;

    owner.dirtyAtMs.store (0);
    owner.requestAnalyze();
}

void SourceGloProcessor::updateTrackProperties (const TrackProperties& properties)
{
    {
        const juce::ScopedLock sl (trackNameLock);
        hostTrackName = properties.name.value_or (juce::String());
    }
    analysisChanged.sendChangeMessage();       // refresh the source panel
}

juce::String SourceGloProcessor::getInputDisplayName() const
{
    {
        const juce::ScopedLock sl (trackNameLock);
        if (hostTrackName.isNotEmpty())
            return hostTrackName;
    }
    return getMainBusNumInputChannels() >= 2 ? "Stereo In" : "Mono In";
}

juce::String SourceGloProcessor::getOutputDisplayName() const
{
    return getMainBusNumOutputChannels() >= 2 ? "Stereo Out" : "Mono Out";
}

void SourceGloProcessor::togglePreview (const juce::String& path)
{
    if (previewPlayer.getActivePath() == path)
        previewPlayer.stop();
    else
        previewPlayer.start (juce::File (path), previewFormats);
}

void SourceGloProcessor::refreshRescues()
{
    const int type = (int) apvts.getRawParameterValue (pid::sourceType)->load();
    analysis.rescues = library.match (type);
    analysisChanged.sendChangeMessage();
}

void SourceGloProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Library scan progressed or favourites changed: refresh the rescue list
    // when Auto Match is on (or when the list has never been filled).
    if (apvts.getRawParameterValue (pid::autoMatch)->load() > 0.5f
         || analysis.rescues.empty())
        refreshRescues();
}

void SourceGloProcessor::requestFixSource()
{
    // Toggle: engage the correction computed from the last analysis, or
    // release it. Does nothing until something has been analysed.
    if (! analysis.analyzed)
        return;

    if (fixChain.isFixEngaged())
        fixChain.disengageFix();
    else
    {
        fixChain.engageFix (lastAnalysis);
        // Hearing the fix means being on the processed side of A/B.
        compareRaw.store (false);

        // The chain is nonlinear (the saturator compresses less at lower
        // drive), so a linear trim estimate undershoots on hot sources.
        // Measure the engaged chain's actual true peak and tighten until the
        // -1 dBTP ceiling really holds.
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            auto processed = makeProcessedSnapshot();
            if (processed.getNumSamples() == 0)
                break;

            PolyphasePeakDetector tpL, tpR;
            float tp = 0.0f;
            const float* l = processed.getReadPointer (0);
            const float* r = processed.getNumChannels() > 1 ? processed.getReadPointer (1) : l;
            for (int i = 0; i < processed.getNumSamples(); ++i)
                tp = juce::jmax (tp, tpL.peakForSample (l[i]), tpR.peakForSample (r[i]));

            const float tpDb = juce::Decibels::gainToDecibels (tp, -120.0f);
            if (tpDb <= -0.5f)
                break;
            fixChain.addTrimDb (-(tpDb + 1.0f));
        }
    }

    analysisChanged.sendChangeMessage();

    // Show the result immediately: the pods re-score the processed source.
    dirtyAtMs.store (0);
    requestAnalyze();
}

float SourceGloProcessor::truePeakSinceDb()
{
    const float linear = truePeakLinear.exchange (0.0f);
    return juce::Decibels::gainToDecibels (linear, -120.0f);
}

// -----------------------------------------------------------------------------
//  State
// -----------------------------------------------------------------------------
void SourceGloProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", presetBank->getCurrentName(), nullptr);
    state.setProperty ("uiScale", uiScale.load(), nullptr);

    const auto fix = fixChain.getFixState();
    juce::String bands;
    for (int b = 0; b < FixChain::numFixBands; ++b)
        bands << juce::String (fix.bandGainDb[b], 3) << (b < FixChain::numFixBands - 1 ? ";" : "");
    state.setProperty ("fixEngaged", fixChain.isFixEngaged(), nullptr);
    state.setProperty ("fixBands", bands, nullptr);
    state.setProperty ("fixTrim", fix.trimDb, nullptr);
    state.setProperty ("fixLowMono", fix.lowMono, nullptr);
    state.setProperty ("fixDc", fix.dcFilter, nullptr);

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
            uiScale.store ((float) (double) state.getProperty ("uiScale", 1.0));

            FixChain::FixState fix;
            auto bands = juce::StringArray::fromTokens (
                state.getProperty ("fixBands", "").toString(), ";", "");
            for (int b = 0; b < FixChain::numFixBands && b < bands.size(); ++b)
                fix.bandGainDb[b] = bands[b].getFloatValue();
            fix.trimDb   = (float) (double) state.getProperty ("fixTrim", 0.0);
            fix.lowMono  = (bool) state.getProperty ("fixLowMono", false);
            fix.dcFilter = (bool) state.getProperty ("fixDc", false);
            fixChain.setFixState (fix, (bool) state.getProperty ("fixEngaged", false));

            apvts.replaceState (state);
            presetBank->notePresetRestored (state.getProperty ("presetName", "").toString());
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
