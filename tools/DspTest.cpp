// -----------------------------------------------------------------------------
//  SourceGlo Pro - deterministic test suite (UI milestone scope).
//
//  What this proves at this milestone:
//    * the parameter contract matches Spec/.../08_LAYOUT/parameters.json,
//      read back from the JSON at runtime - code drift fails the test
//    * gain / phase / mono / bypass audio behaviour, with absolute targets
//    * state save/load round-trip including the UI scale
//    * editor open/close is stable and every control has an accessible name
//    * all supplied artwork loads
//
//  Grows with the DSP milestones; the harness pattern is EQGlo Pro's.
// -----------------------------------------------------------------------------

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <cstdio>

using namespace sourceglo;

static int checks = 0, failures = 0;

static void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL: %s\n", what.toRawUTF8());
    }
}

static void checkNear (double value, double target, double tol, const juce::String& what)
{
    check (std::abs (value - target) <= tol,
           what + " (got " + juce::String (value, 4) + ", want "
                + juce::String (target, 4) + " +/- " + juce::String (tol, 4) + ")");
}

// --- feed a sine block and return output RMS ---------------------------------
static double processSineRms (SourceGloProcessor& p, double freq, float amp,
                              int blocks = 30, bool* phaseFlipped = nullptr)
{
    const double sr = 48000.0;
    const int blockSize = 512;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;
    double sumSq = 0.0;
    int count = 0;
    bool flipped = true;

    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> dry (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float v = amp * (float) std::sin (phase);
            phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
            dry.setSample (0, i, v);
            dry.setSample (1, i, v);
        }

        p.processBlock (buffer, midi);

        if (b >= blocks / 2)     // let smoothing settle
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float out = buffer.getSample (0, i);
                sumSq += (double) out * out;
                ++count;
                if (dry.getSample (0, i) * out > 1.0e-9f)
                    flipped = false;
            }
        }
    }

    if (phaseFlipped != nullptr)
        *phaseFlipped = flipped;
    return std::sqrt (sumSq / juce::jmax (1, count));
}

static void setParam (SourceGloProcessor& p, const char* id, float value)
{
    auto* param = p.getAPVTS().getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

// -----------------------------------------------------------------------------
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("SourceGlo Pro test suite\n========================\n");

    // Sandbox the library index so no test ever touches the user's real one.
    auto testRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("SourceGloTests");
    testRoot.deleteRecursively();
    testRoot.createDirectory();
    RescueLibrary::indexFileOverride() = testRoot.getChildFile ("LibraryIndex.json");
    PresetBank::userDirOverride() = testRoot.getChildFile ("UserPresets");

    // ---------------------------------------------------------------- assets
    std::printf ("- artwork\n");
    {
        check (Assets::assetsDirectory().isDirectory(), "assets directory found");
        check (Assets::shell().isValid(), "shell loads");
        check (Assets::logoHeader (1.0f).isValid(), "header logo loads");
        check (Assets::logoHeader (1.0f).getWidth() == 320
                && Assets::logoHeader (1.0f).getHeight() == 42, "header logo is the 320x42 export");
        check (Assets::scoreRingBase().isValid(), "score ring base loads");
        check (Assets::macroKnob().isValid()
                && (int) Assets::macroKnob().frames.size() == 128, "macro filmstrip slices to 128 frames");
        check (Assets::macroKnob().frames[0].getWidth() == 96, "macro frame is 96 px");
        check (Assets::trimKnob().isValid()
                && (int) Assets::trimKnob().frames.size() == 128, "trim filmstrip slices to 128 frames");
        for (int k = 0; k < 6; ++k)
            for (int s = 0; s < 4; ++s)
                check (Assets::button ((ButtonKind) k, (ButtonState) s).isValid(),
                       "button art kind " + juce::String (k) + " state " + juce::String (s));
        for (int i = 0; i < 3; ++i)
            check (Assets::diagnosticCard (i).isValid(), "diagnostic card " + juce::String (i));
        for (int i = 0; i < 3; ++i)
            check (Assets::rescueRow (i).isValid(), "rescue row " + juce::String (i));
        check (Assets::spectrumGrid().isValid(), "spectrum grid loads");
        check (Assets::radarGrid().isValid(), "radar grid loads");
        for (const char* phrase : { "READY", "GOOD", "NEEDS WORK", "FIX REQUIRED" })
            check (Assets::statusPill (phrase).isValid(),
                   juce::String ("status pill for ") + phrase);
        check (Assets::icon ("analyze", juce::Colours::white) != nullptr, "icons parse");
    }

    // ---------------------------------------------- parameter contract vs JSON
    std::printf ("- parameter contract vs Spec parameters.json\n");
    {
        SourceGloProcessor p;

        juce::File specFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        for (int i = 0; i < 6 && ! specFile.getChildFile ("Spec").isDirectory(); ++i)
            specFile = specFile.getParentDirectory();
        auto json = juce::JSON::parse (specFile.getChildFile (
            "Spec/SourceGlo_Pro_UI_Assets_v1.1/08_LAYOUT/parameters.json").loadFileAsString());

        auto* list = json.getArray();
        check (list != nullptr, "parameters.json parses");

        if (list != nullptr)
        {
            check (list->size() == 17, "spec defines 17 parameters");
            for (const auto& entry : *list)
            {
                const auto id   = entry["id"].toString();
                const auto type = entry["type"].toString();
                auto* param = p.getAPVTS().getParameter (id);
                check (param != nullptr, "parameter exists: " + id);
                if (param == nullptr)
                    continue;

                if (type == "float")
                {
                    auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param);
                    checkNear (rp->getNormalisableRange().start, (double) entry["min"], 1e-6,
                               id + " min");
                    checkNear (rp->getNormalisableRange().end, (double) entry["max"], 1e-6,
                               id + " max");
                    checkNear ((double) rp->convertFrom0to1 (rp->getDefaultValue()),
                               (double) entry["default"], 1e-4, id + " default");
                }
                else if (type == "bool")
                {
                    const bool def = (bool) entry["default"];
                    checkNear (param->getDefaultValue(), def ? 1.0 : 0.0, 1e-6, id + " default");
                }
                else if (type == "choice")
                {
                    auto* cp = dynamic_cast<juce::AudioParameterChoice*> (param);
                    check (cp != nullptr, id + " is a choice");
                    if (cp != nullptr)
                    {
                        auto* choices = entry["choices"].getArray();
                        check (choices != nullptr
                                && cp->choices.size() == choices->size(),
                               id + " choice count");
                        if (choices != nullptr)
                            for (int c = 0; c < choices->size() && c < cp->choices.size(); ++c)
                                check (cp->choices[c] == (*choices)[c].toString(),
                                       id + " choice " + juce::String (c));
                        auto* base = static_cast<juce::RangedAudioParameter*> (cp);
                        const int defIndex = (int) (base->getDefaultValue()
                                * (float) (cp->choices.size() - 1) + 0.5f);
                        check (cp->choices[defIndex] == entry["default"].toString(),
                               id + " default choice");
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------ audio
    std::printf ("- audio path\n");
    {
        SourceGloProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        // The input/output/phase/mono/bypass contract is measured with the
        // correction chain neutral - the chain's own stages have their own
        // section below. (The parameter defaults intentionally colour.)
        setParam (p, pid::punch, 0.0f);
        setParam (p, pid::body, 0.0f);
        setParam (p, pid::tone, 50.0f);
        setParam (p, pid::air, 0.0f);
        setParam (p, pid::stereo, 0.0f);
        setParam (p, pid::transients, 0.0f);
        setParam (p, pid::saturate, 0.0f);

        // Unity: -20 dBFS sine in -> same RMS out (sine RMS = amp/sqrt2).
        const double unityRms = processSineRms (p, 1000.0, 0.1f);
        checkNear (juce::Decibels::gainToDecibels (unityRms),
                   juce::Decibels::gainToDecibels (0.1 / juce::MathConstants<double>::sqrt2),
                   0.05, "unity gain RMS");

        // +6 dB input trim.
        setParam (p, pid::inputGain, 6.0f);
        const double plus6 = processSineRms (p, 1000.0, 0.1f);
        checkNear (juce::Decibels::gainToDecibels (plus6 / unityRms), 6.0, 0.1,
                   "+6 dB input gain");
        setParam (p, pid::inputGain, 0.0f);

        // -6 dB output trim.
        setParam (p, pid::outputGain, -6.0f);
        const double minus6 = processSineRms (p, 1000.0, 0.1f);
        checkNear (juce::Decibels::gainToDecibels (minus6 / unityRms), -6.0, 0.1,
                   "-6 dB output gain");
        setParam (p, pid::outputGain, 0.0f);

        // Phase invert flips polarity without changing level.
        setParam (p, pid::phaseInvert, 1.0f);
        bool flipped = false;
        const double invRms = processSineRms (p, 1000.0, 0.1f, 30, &flipped);
        check (flipped, "phase invert flips polarity");
        checkNear (invRms / unityRms, 1.0, 0.02, "phase invert keeps level");
        setParam (p, pid::phaseInvert, 0.0f);

        // Mono: L-only input becomes equal L/R halves.
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            setParam (p, pid::mono, 1.0f);
            for (int b = 0; b < 12; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    buffer.setSample (0, i, 0.5f);
                    buffer.setSample (1, i, 0.0f);
                }
                p.processBlock (buffer, midi);
            }
            checkNear (buffer.getSample (0, 500), 0.25, 0.01, "mono sums L");
            checkNear (buffer.getSample (1, 500), 0.25, 0.01, "mono sums R");
            setParam (p, pid::mono, 0.0f);
        }

        // Bypass passes input through untouched even with gain cranked.
        setParam (p, pid::inputGain, 12.0f);
        setParam (p, pid::bypass, 1.0f);
        const double bypassRms = processSineRms (p, 1000.0, 0.1f);
        checkNear (bypassRms / unityRms, 1.0, 0.02, "bypass is unity");
        setParam (p, pid::bypass, 0.0f);
        setParam (p, pid::inputGain, 0.0f);

        // Meters saw signal.
        check (p.inPeak[0].load() > 0.01f, "input meter tap alive");
        check (p.outPeak[0].load() > 0.01f, "output meter tap alive");
    }

    // ----------------------------------------------------------------- engine
    std::printf ("- analysis engine (ground-truth fixtures)\n");
    {
        const double sr = 48000.0;

        auto makeSine = [&] (double freq, float amp, double seconds, double phase = 0.0)
        {
            juce::AudioBuffer<float> b (2, (int) (sr * seconds));
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                          * freq * i / sr + phase);
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
            return b;
        };

        auto makeKickPattern = [&] (double bpm, double seconds)
        {
            juce::AudioBuffer<float> b (2, (int) (sr * seconds));
            const double beat = 60.0 / bpm;
            double phase = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                const double beatPos = std::fmod (t, beat);
                const float env = (float) std::exp (-beatPos * 12.0);
                phase += 2.0 * juce::MathConstants<double>::pi * (55.0 + 30.0 * env) / sr;
                const float v = 0.8f * env * (float) std::sin (phase);
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
            return b;
        };

        auto makeTriad = [&] (double f1, double f2, double f3, double seconds)
        {
            juce::AudioBuffer<float> b (2, (int) (sr * seconds));
            b.clear();
            const double freqs[] = { f1, f2, f3, f1 * 2.0, f2 * 2.0, f3 * 2.0 };
            const float amps[]   = { 0.30f, 0.22f, 0.22f, 0.12f, 0.08f, 0.08f };
            for (int v = 0; v < 6; ++v)
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float x = amps[v] * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                  * freqs[v] * i / sr);
                    b.addSample (0, i, x);
                    b.addSample (1, i, x);
                }
            return b;
        };

        // --- stats against exact values: 1 kHz sine at -6.02 dBFS.
        {
            const auto r = AnalysisEngine::analyse (makeSine (1000.0, 0.5f, 3.0), sr, 0);
            check (r.enoughAudio, "sine: enough audio");
            checkNear (r.peakDb, -6.02, 0.15, "sine peak");
            checkNear (r.rmsDb, -9.03, 0.15, "sine RMS");
            checkNear (r.crestDb, 3.01, 0.25, "sine crest");
            checkNear (r.truePeakDb, -6.02, 0.35, "sine true peak");
            checkNear (r.durationSeconds, 3.0, 0.1, "sine duration");
        }

        // --- true peak absolute: full-scale fs/4 sine at 45 degrees puts
        //     every sample on +/-0.7071 while the waveform touches 1.0.
        {
            const auto r = AnalysisEngine::analyse (
                makeSine (sr / 4.0, 1.0f, 2.0, juce::MathConstants<double>::pi / 4.0), sr, 0);
            checkNear (r.peakDb, -3.01, 0.1, "fs/4 sample peak reads -3");
            checkNear (r.truePeakDb, 0.0, 0.35, "fs/4 true peak reads 0 dBTP");
        }

        // --- tempo from kick patterns.
        {
            const auto r128 = AnalysisEngine::analyse (makeKickPattern (128.0, 6.0), sr, 1);
            checkNear (r128.tempoBpm, 128.0, 2.0, "tempo 128 BPM detected");

            const auto r92 = AnalysisEngine::analyse (makeKickPattern (92.0, 6.0), sr, 1);
            checkNear (r92.tempoBpm, 92.0, 2.0, "tempo 92 BPM detected");
        }

        // --- key from triads (C minor: C-Eb-G, A major: A-C#-E).
        {
            const auto cm = AnalysisEngine::analyse (makeTriad (130.81, 155.56, 196.00, 4.0), sr, 9);
            check (cm.keyName == "C Minor", "C minor triad -> C Minor (got '"
                                              + cm.keyName + "')");

            const auto am = AnalysisEngine::analyse (makeTriad (110.00, 138.59, 164.81, 4.0), sr, 9);
            check (am.keyName == "A Major", "A major triad -> A Major (got '"
                                              + am.keyName + "')");
        }

        // --- clipping detection.
        {
            auto clipped = makeSine (60.0, 1.4f, 2.0);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = clipped.getWritePointer (ch);
                for (int i = 0; i < clipped.getNumSamples(); ++i)
                    d[i] = juce::jlimit (-1.0f, 1.0f, d[i]);
            }
            const auto rc = AnalysisEngine::analyse (clipped, sr, 1);
            bool foundClip = false, foundClean = false;
            for (const auto& d : rc.diagnostics)
                if (d.title == "Digital Clipping Detected") foundClip = true;
            check (foundClip, "clipped signal raises the clipping diagnostic");

            const auto rq = AnalysisEngine::analyse (makeSine (60.0, 0.5f, 2.0), sr, 1);
            for (const auto& d : rq.diagnostics)
                if (d.title == "Clipping Clean") foundClean = true;
            check (foundClean, "clean signal reports Clipping Clean");
        }

        // --- phase: dual mono vs polarity-flipped right channel.
        {
            juce::AudioBuffer<float> noise (2, (int) (sr * 2.0));
            juce::Random rng (0x5eed);
            for (int i = 0; i < noise.getNumSamples(); ++i)
            {
                const float v = rng.nextFloat() * 0.8f - 0.4f;
                noise.setSample (0, i, v);
                noise.setSample (1, i, v);
            }
            const auto mono = AnalysisEngine::analyse (noise, sr, 0);
            check (mono.phase >= 90, "dual mono scores phase >= 90 (got "
                                       + juce::String (mono.phase) + ")");

            for (int i = 0; i < noise.getNumSamples(); ++i)
                noise.setSample (1, i, -noise.getSample (0, i));
            const auto flipped = AnalysisEngine::analyse (noise, sr, 0);
            check (flipped.enoughAudio, "flipped polarity is analysed, not gated as silence");
            check (flipped.phase <= 20, "flipped polarity scores phase <= 20 (got "
                                          + juce::String (flipped.phase) + ")");
            bool foundPhase = false;
            for (const auto& d : flipped.diagnostics)
                if (d.title == "Phase Cancellation Risk") foundPhase = true;
            check (foundPhase, "flipped polarity raises the phase diagnostic");
        }

        // --- not enough audio.
        {
            const auto r = AnalysisEngine::analyse (makeSine (200.0, 0.5f, 0.2), sr, 0);
            check (! r.enoughAudio, "0.2 s of audio is not enough to analyse");
            check (! r.diagnostics.empty()
                     && r.diagnostics[0].title == "Not Enough Audio",
                   "not-enough-audio diagnostic present");
        }

        // --- tone ordering: a kick-shaped source beats white noise as a Kick.
        {
            juce::AudioBuffer<float> noise (2, (int) (sr * 3.0));
            juce::Random rng (0xa153);
            for (int i = 0; i < noise.getNumSamples(); ++i)
            {
                const float v = rng.nextFloat() * 1.0f - 0.5f;
                noise.setSample (0, i, v);
                noise.setSample (1, i, v);
            }
            const auto kick = AnalysisEngine::analyse (makeKickPattern (128.0, 3.0), sr, 1);
            const auto hiss = AnalysisEngine::analyse (noise, sr, 1);
            check (kick.tone > hiss.tone,
                   "kick fixture out-scores white noise on Kick tone ("
                     + juce::String (kick.tone) + " vs " + juce::String (hiss.tone) + ")");
        }

        // --- sub conflict: a pure 50 Hz sine is on-target for a Kick but
        //     sub-heavy for a Loop.
        {
            const auto asKick = AnalysisEngine::analyse (makeSine (50.0, 0.5f, 2.0), sr, 1);
            check (asKick.conflictHiHz <= asKick.conflictLoHz,
                   "pure sub as Kick raises no conflict overlay");

            const auto asLoop = AnalysisEngine::analyse (makeSine (50.0, 0.5f, 2.0), sr, 8);
            check (asLoop.conflictHiHz > asLoop.conflictLoHz,
                   "pure sub as Loop raises the low-end conflict overlay");
        }

        // --- determinism.
        {
            const auto buffer = makeKickPattern (120.0, 3.0);
            const auto a = AnalysisEngine::analyse (buffer, sr, 1);
            const auto b = AnalysisEngine::analyse (buffer, sr, 1);
            check (a.score == b.score && a.tone == b.tone && a.tempoBpm == b.tempoBpm,
                   "identical audio gives identical results");
        }

        // --- full publish path through the processor.
        {
            SourceGloProcessor p;
            p.setPlayConfigDetails (2, 2, 48000.0, 512);
            p.prepareToPlay (48000.0, 512);

            check (! p.getAnalysis().analyzed, "model starts unanalysed");

            auto feed = makeKickPattern (128.0, 4.0);
            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            for (int start = 0; start + 512 <= feed.getNumSamples(); start += 512)
            {
                for (int ch = 0; ch < 2; ++ch)
                    block.copyFrom (ch, 0, feed, ch, start, 512);
                p.processBlock (block, midi);
            }

            p.analyzeNow();
            const auto& m = p.getAnalysis();
            check (m.analyzed, "analyzeNow marks the model analysed");
            check (m.score >= 0 && m.score <= 100, "score in range");
            check (m.stats.durationSec > 3.0f, "duration captured");
            check (! m.diagnostics.empty(), "diagnostics produced");

            // The tab views read the full band picture from the model.
            bool loudestFound = false;
            for (int band = 0; band < 5; ++band)
            {
                check (m.bandLevelDb[band] <= 0.01f, "band level re loudest is <= 0");
                if (std::abs (m.bandLevelDb[band]) < 0.01f)
                    loudestFound = true;
            }
            check (loudestFound, "one band is the loudest reference");
            check (m.bandLevelDb[0] > -10.0f, "kick fixture is sub-heavy in the model");
            check (std::abs (m.correlation - 1.0f) < 0.05f,
                   "dual-mono fixture reports full correlation");
        }
    }

    // -------------------------------------------------------------- fix chain
    std::printf ("- fix chain and macros (absolute stage targets)\n");
    {
        const double sr = 48000.0;
        const int blockSize = 512;

        // Measure the chain's steady-state gain for a sine at freq with the
        // given macro settings (last half of 1.5 s, chain fed block-wise).
        auto chainGainDb = [&] (double freq, FixChain::MacroValues m,
                                FixChain* preloaded = nullptr) -> double
        {
            FixChain local;
            FixChain& chain = preloaded != nullptr ? *preloaded : local;
            if (preloaded == nullptr)
                chain.prepare (sr, blockSize);

            juce::AudioBuffer<float> block (2, blockSize);
            double phase = 0.0, sumSq = 0.0;
            int count = 0;
            const int blocks = (int) (sr * 1.5 / blockSize);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const float v = 0.25f * (float) std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
                    block.setSample (0, i, v);
                    block.setSample (1, i, v);
                }
                chain.process (block, m);
                if (b >= blocks / 2)
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const double v = block.getSample (0, i);
                        sumSq += v * v; ++count;
                    }
            }
            const double rms = std::sqrt (sumSq / count);
            return juce::Decibels::gainToDecibels (rms / (0.25 / juce::MathConstants<double>::sqrt2));
        };

        FixChain::MacroValues neutral;
        neutral.punch = neutral.body = neutral.air = neutral.stereo
            = neutral.transients = neutral.saturate = 0.0f;
        neutral.tone = 0.5f;

        // --- neutral chain is transparent.
        checkNear (chainGainDb (1000.0, neutral), 0.0, 0.1, "neutral chain is unity at 1 kHz");
        checkNear (chainGainDb (100.0, neutral), 0.0, 0.1, "neutral chain is unity at 100 Hz");

        // --- Body: +6 dB bell at 180 Hz, transparent far away.
        {
            auto m = neutral; m.body = 1.0f;
            checkNear (chainGainDb (180.0, m), 6.0, 0.75, "Body 100 -> +6 dB at 180 Hz");
            checkNear (chainGainDb (4000.0, m), 0.0, 0.75, "Body 100 leaves 4 kHz alone");
        }

        // --- Tone: tilt around the mids.
        {
            auto m = neutral; m.tone = 1.0f;
            checkNear (chainGainDb (8000.0, m), 4.5, 1.0, "Tone 100 -> +4.5 dB up top");
            checkNear (chainGainDb (100.0, m), -4.5, 1.0, "Tone 100 -> -4.5 dB down low");
            m.tone = 0.0f;
            checkNear (chainGainDb (8000.0, m), -4.5, 1.0, "Tone 0 -> -4.5 dB up top");
        }

        // --- Air: +6 dB shelf at 12 kHz.
        {
            auto m = neutral; m.air = 1.0f;
            checkNear (chainGainDb (15000.0, m), 6.0, 1.0, "Air 100 -> +6 dB at 15 kHz");
            checkNear (chainGainDb (500.0, m), 0.0, 0.75, "Air 100 leaves 500 Hz alone");
        }

        // --- Saturate: drive 0 is bit-exact dry; full drive keeps level and
        //     generates real odd harmonics.
        {
            FixChain chain;
            chain.prepare (sr, blockSize);
            auto m = neutral; m.saturate = 0.0f;

            juce::AudioBuffer<float> block (2, blockSize);
            double phase = 0.0;
            float worst = 0.0f;
            for (int b = 0; b < 30; ++b)
            {
                juce::AudioBuffer<float> dry (2, blockSize);
                for (int i = 0; i < blockSize; ++i)
                {
                    const float v = 0.5f * (float) std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * 1000.0 / sr;
                    block.setSample (0, i, v); block.setSample (1, i, v);
                    dry.setSample (0, i, v);   dry.setSample (1, i, v);
                }
                chain.process (block, m);
                if (b > 15)
                    for (int i = 0; i < blockSize; ++i)
                        worst = juce::jmax (worst, std::abs (block.getSample (0, i)
                                                              - dry.getSample (0, i)));
            }
            check (worst < 1.0e-4f, "Saturate 0 nulls against dry (worst "
                                      + juce::String (worst, 7) + ")");

            auto hot = neutral; hot.saturate = 1.0f;
            const double levelDb = chainGainDb (1000.0, hot);
            check (std::abs (levelDb) < 2.0, "Saturate 100 holds level within 2 dB (got "
                                               + juce::String (levelDb, 2) + ")");

            // Third harmonic: FFT of the saturated sine.
            FixChain h;
            h.prepare (sr, 4096);
            juce::AudioBuffer<float> big (2, 4096);
            double p2 = 0.0;
            for (int r = 0; r < 8; ++r)     // settle smoothing
            {
                for (int i = 0; i < 4096; ++i)
                {
                    const float v = 0.5f * (float) std::sin (p2);
                    p2 += 2.0 * juce::MathConstants<double>::pi * 750.0 / sr;
                    big.setSample (0, i, v); big.setSample (1, i, v);
                }
                h.process (big, hot);
            }
            juce::dsp::FFT fft (12);
            std::vector<float> data (8192, 0.0f);
            for (int i = 0; i < 4096; ++i)
                data[(size_t) i] = big.getSample (0, i);
            fft.performFrequencyOnlyForwardTransform (data.data());
            const int fundBin = (int) std::round (750.0 * 4096 / sr);
            const float fund = data[(size_t) fundBin];
            const float third = data[(size_t) (fundBin * 3)];
            const float thirdDb = juce::Decibels::gainToDecibels (third / juce::jmax (1.0e-9f, fund));
            check (thirdDb > -40.0f, "Saturate 100 generates a 3rd harmonic above -40 dBc (got "
                                       + juce::String (thirdDb, 1) + ")");
        }

        // --- Stereo width: side grows ~1.6x, mid untouched.
        {
            FixChain chain;
            chain.prepare (sr, blockSize);
            auto m = neutral; m.stereo = 1.0f;

            juce::AudioBuffer<float> block (2, blockSize);
            double phase = 0.0, midSq = 0.0, sideSq = 0.0, midSqIn = 0.0, sideSqIn = 0.0;
            const int blocks = (int) (sr * 1.0 / blockSize);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const float v = 0.4f * (float) std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * 500.0 / sr;
                    block.setSample (0, i, v);
                    block.setSample (1, i, v * 0.4f);
                }
                if (b >= blocks / 2)
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const double mi = 0.5 * (block.getSample (0, i) + block.getSample (1, i));
                        const double si = 0.5 * (block.getSample (0, i) - block.getSample (1, i));
                        midSqIn += mi * mi; sideSqIn += si * si;
                    }
                chain.process (block, m);
                if (b >= blocks / 2)
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const double mi = 0.5 * (block.getSample (0, i) + block.getSample (1, i));
                        const double si = 0.5 * (block.getSample (0, i) - block.getSample (1, i));
                        midSq += mi * mi; sideSq += si * si;
                    }
            }
            checkNear (std::sqrt (sideSq / sideSqIn), 1.6, 0.1, "Stereo 100 widens the side 1.6x");
            checkNear (std::sqrt (midSq / midSqIn), 1.0, 0.05, "Stereo 100 leaves the mid alone");
        }

        // --- fix engagement: counter-EQ, trim, low-mono.
        {
            FixChain chain;
            chain.prepare (sr, blockSize);

            AnalysisResult a;
            a.enoughAudio = true;
            a.bandDeviationDb[3] = 8.0f;      // 6 dB of harsh excess beyond the deadzone
            a.truePeakDb = 0.5f;              // over full scale -> trim to -1 dBTP
            a.lowCorrelation = 1.0f;
            chain.engageFix (a);
            check (chain.isFixEngaged(), "fix engages");

            auto m = neutral; m.fixAmount = 1.0f;
            const double at3500 = chainGainDb (3500.0, m, &chain);
            // Full counter of the (capped) +8 dB deviation, minus 1.5 dB trim.
            checkNear (at3500, -9.5, 1.3, "fix counters a +8 dB HighMid deviation at full amount");

            FixChain chain50;
            chain50.prepare (sr, blockSize);
            chain50.engageFix (a);
            auto mHalf = neutral; mHalf.fixAmount = 0.5f;
            const double at3500half = chainGainDb (3500.0, mHalf, &chain50);
            checkNear (at3500half, -4.75, 1.3, "fix amount 50% halves the correction");

            chain.disengageFix();
            check (! chain.isFixEngaged(), "fix disengages");
        }

        // --- engaging the fix snaps A/B back to the processed side.
        {
            SourceGloProcessor p;
            p.setPlayConfigDetails (2, 2, 48000.0, 512);
            p.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;
            double phase = 0.0;
            for (int b = 0; b < 60; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.4f * (float) std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * 100.0 / 48000.0;
                    block.setSample (0, i, v);
                    block.setSample (1, i, v);
                }
                p.processBlock (block, midi);
            }
            p.analyzeNow();
            p.setCompareRaw (true);
            p.requestFixSource();
            check (p.isFixEngaged(), "fix engages from an analysis");
            check (! p.isComparingRaw(), "engaging the fix snaps A/B to the processed side");
        }

        // --- low-mono fix kills wide sub content.
        {
            FixChain chain;
            chain.prepare (sr, blockSize);
            AnalysisResult a;
            a.enoughAudio = true;
            a.lowCorrelation = -0.5f;
            chain.engageFix (a);

            auto m = neutral; m.fixAmount = 1.0f;
            juce::AudioBuffer<float> block (2, blockSize);
            double phase = 0.0, sideSq = 0.0, sideSqIn = 0.0;
            const int blocks = (int) (sr * 1.5 / blockSize);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const float v = 0.4f * (float) std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * 45.0 / sr;
                    block.setSample (0, i, v);
                    block.setSample (1, i, -v);        // fully wide sub
                }
                if (b >= blocks / 2)
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const double si = 0.5 * (block.getSample (0, i) - block.getSample (1, i));
                        sideSqIn += si * si;
                    }
                chain.process (block, m);
                if (b >= blocks / 2)
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const double si = 0.5 * (block.getSample (0, i) - block.getSample (1, i));
                        sideSq += si * si;
                    }
            }
            const double reductionDb = 10.0 * std::log10 (sideSq / juce::jmax (1.0e-12, sideSqIn));
            check (reductionDb < -12.0, "low-mono fix cuts 45 Hz side by > 12 dB (got "
                                          + juce::String (reductionDb, 1) + ")");
        }

        // --- transient shaping: attack of a burst gains more than its tail.
        {
            FixChain chain;
            chain.prepare (sr, blockSize);
            auto m = neutral; m.transients = 1.0f;

            const int n = (int) (sr * 2.0);
            juce::AudioBuffer<float> in (2, n), out (2, n);
            double phase = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double t = std::fmod (i / sr, 0.5);
                const float env = t < 0.25f ? 1.0f : 0.0f;   // 250 ms bursts
                const float v = 0.35f * env * (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * 400.0 / sr;
                in.setSample (0, i, v);
                in.setSample (1, i, v);
            }
            juce::AudioBuffer<float> block (2, blockSize);
            for (int start = 0; start + blockSize <= n; start += blockSize)
            {
                for (int ch = 0; ch < 2; ++ch)
                    block.copyFrom (ch, 0, in, ch, start, blockSize);
                chain.process (block, m);
                for (int ch = 0; ch < 2; ++ch)
                    out.copyFrom (ch, start, block, ch, 0, blockSize);
            }

            // Compare gain over the first 10 ms of the last burst vs its tail.
            const int burstStart = (int) (sr * 1.5);
            auto rmsOf = [] (const juce::AudioBuffer<float>& b, int from, int len)
            {
                double sum = 0.0;
                for (int i = from; i < from + len; ++i)
                    sum += (double) b.getSample (0, i) * b.getSample (0, i);
                return std::sqrt (sum / len);
            };
            const int ms10 = (int) (sr * 0.010), ms100 = (int) (sr * 0.100);
            const double attackGain = rmsOf (out, burstStart, ms10) / rmsOf (in, burstStart, ms10);
            const double tailGain = rmsOf (out, burstStart + ms100, ms100)
                                      / rmsOf (in, burstStart + ms100, ms100);
            check (attackGain > tailGain * 1.25,
                   "Transients 100 lifts the attack over the tail ("
                     + juce::String (attackGain, 2) + " vs " + juce::String (tailGain, 2) + ")");
            check (tailGain < 1.15, "Transients 100 leaves the tail nearly alone (got "
                                      + juce::String (tailGain, 2) + ")");
        }

        // --- A/B compare returns the raw side.
        {
            FixChain chain;
            chain.prepare (sr, blockSize);
            auto m = neutral; m.body = 1.0f; m.saturate = 1.0f; m.compare = true;
            const double gain = chainGainDb (180.0, m, &chain);
            checkNear (gain, 0.0, 0.15, "A/B raw side bypasses the chain");
        }

        // --- reported oversampling latency tracks the parameter.
        {
            SourceGloProcessor p;
            p.setPlayConfigDetails (2, 2, 48000.0, 512);
            p.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;

            setParam (p, pid::oversampling, 0.0f);   // Off
            block.clear(); p.processBlock (block, midi);
            check (p.getLatencySamples() == 0, "latency 0 with oversampling off");

            setParam (p, pid::oversampling, 3.0f);   // 8x
            block.clear(); p.processBlock (block, midi);
            check (p.getLatencySamples() > 0, "8x oversampling reports latency (got "
                                                + juce::String (p.getLatencySamples()) + ")");
        }
    }

    // ---------------------------------------------------------------- library
    std::printf ("- rescue library\n");
    {
        const double sr = 48000.0;
        auto sampleDir = testRoot.getChildFile ("samples");
        sampleDir.createDirectory();

        // Fixture generator: write a mono wav.
        auto writeWav = [&] (const juce::String& name,
                             std::function<float (int)> gen, double seconds)
        {
            juce::WavAudioFormat wav;
            auto file = sampleDir.getChildFile (name);
            auto stream = file.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (stream.get(), sr, 1, 16, {}, 0));
            check (writer != nullptr, "wav writer for " + name);
            if (writer == nullptr)
                return;
            stream.release();     // writer owns it now

            const int n = (int) (sr * seconds);
            juce::AudioBuffer<float> b (1, n);
            for (int i = 0; i < n; ++i)
                b.setSample (0, i, gen (i));
            writer->writeFromAudioSampleBuffer (b, 0, n);
        };

        juce::Random rng (0x11b);
        float hpState = 0.0f;
        writeWav ("kick_a.wav", [&] (int i)
        {
            const float env = std::exp (-4.0f * (float) i / (float) sr / 0.5f * 4.0f);
            return 0.9f * env * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * i / sr);
        }, 0.5);
        writeWav ("kick_b.wav", [&] (int i)
        {
            const float env = std::exp (-3.0f * (float) i / (float) sr / 0.7f * 4.0f);
            return 0.8f * env * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / sr);
        }, 0.7);
        writeWav ("hat.wav", [&] (int i)
        {
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            const float hp = white - hpState;
            hpState = white;
            juce::ignoreUnused (i);
            return 0.5f * hp;
        }, 0.25);
        writeWav ("sub808.wav", [&] (int i)
        {
            return 0.7f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 45.0 * i / sr);
        }, 2.5);
        writeWav ("pad.wav", [&] (int i)
        {
            return 0.2f * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr)
                                 + std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * i / sr)
                                 + std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr));
        }, 4.0);

        // --- per-file analysis ground truth.
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            LibraryEntry e;
            check (RescueLibrary::analyseFile (sampleDir.getChildFile ("kick_a.wav"), fm, e),
                   "kick_a analyses");
            check (e.bandLevelDb[0] == 0.0f, "kick_a is sub-dominant");
            checkNear (e.durationSec, 0.5, 0.05, "kick_a duration");

            LibraryEntry h;
            check (RescueLibrary::analyseFile (sampleDir.getChildFile ("hat.wav"), fm, h),
                   "hat analyses");
            check (h.bandLevelDb[4] == 0.0f || h.bandLevelDb[3] == 0.0f,
                   "hat is high-dominant");
        }

        // --- scan + match + persistence.
        {
            RescueLibrary lib;
            lib.addFolder (sampleDir);

            int waited = 0;
            while (lib.isScanning() && waited < 400) { juce::Thread::sleep (25); ++waited; }
            check (! lib.isScanning(), "scan finishes");
            check (lib.getIndexedCount() == 5, "all five fixtures indexed (got "
                                                 + juce::String (lib.getIndexedCount()) + ")");
            check (RescueLibrary::indexFile().existsAsFile(), "index persisted to disk");

            const auto forKick = lib.match (1);
            check ((int) forKick.size() == 5, "match returns the full top list");
            for (size_t i = 1; i < forKick.size(); ++i)
                check (forKick[i - 1].fitPercent >= forKick[i].fitPercent,
                       "fit sorted descending");
            check (forKick.back().fileName == "hat.wav",
                   "hat ranks last for a Kick (got last = " + forKick.back().fileName + ")");
            check (forKick.front().fileName != "hat.wav"
                     && forKick.front().fileName != "pad.wav",
                   "a low-end one-shot ranks first for a Kick (got "
                     + forKick.front().fileName + ")");
            for (const auto& sugg : forKick)
                check (sugg.fitPercent >= 1 && sugg.fitPercent <= 99, "fit in range");

            const auto again = lib.match (1);
            check (again.front().path == forKick.front().path
                     && again.back().path == forKick.back().path,
                   "matching is deterministic");

            const auto forHatType = lib.match (6);
            check (forHatType.front().fileName == "hat.wav",
                   "hat ranks first for the Hat type (got " + forHatType.front().fileName + ")");

            // Favourites persist through the index file.
            lib.setFavourite (sampleDir.getChildFile ("pad.wav").getFullPathName(), true);
        }
        {
            RescueLibrary reloaded;
            check (reloaded.getIndexedCount() == 5, "index reloads from disk");
            const auto matched = reloaded.match (9, 5);
            bool padFav = false;
            for (const auto& s2 : matched)
                if (s2.fileName == "pad.wav" && s2.favourite)
                    padFav = true;
            check (padFav, "favourite survives reload");
        }

        // --- preview audition mixes into the output and stops.
        {
            SourceGloProcessor p;
            p.setPlayConfigDetails (2, 2, 48000.0, 512);
            p.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> block (2, 512);
            juce::MidiBuffer midi;

            p.togglePreview (sampleDir.getChildFile ("sub808.wav").getFullPathName());
            check (p.getPreviewPath().isNotEmpty(), "preview reports active");

            float heard = 0.0f;
            for (int b = 0; b < 20; ++b)
            {
                block.clear();
                p.processBlock (block, midi);
                heard = juce::jmax (heard, block.getMagnitude (0, 0, 512));
            }
            check (heard > 0.05f, "preview is audible on silent input (peak "
                                    + juce::String (heard, 3) + ")");

            p.togglePreview (sampleDir.getChildFile ("sub808.wav").getFullPathName());
            for (int b = 0; b < 4; ++b) { block.clear(); p.processBlock (block, midi); }
            block.clear();
            p.processBlock (block, midi);
            check (block.getMagnitude (0, 0, 512) < 1.0e-4f, "preview stops silent");
            check (p.getPreviewPath().isEmpty(), "preview reports stopped");
        }
    }

    // ---------------------------------------------------------------- presets
    std::printf ("- preset bank\n");
    {
        SourceGloProcessor p;
        auto& bank = p.getPresets();

        check (bank.getNumPresets() == 29, "29 factory presets (got "
                                             + juce::String (bank.getNumPresets()) + ")");
        check (bank.getCurrentName() == "Punchy Kick Starter",
               "fresh instance opens on Punchy Kick Starter (got '"
                 + bank.getCurrentName() + "')");
        check (! bank.isModified(), "fresh instance starts clean");
        checkNear ((double) p.getAPVTS().getRawParameterValue (pid::sourceType)->load(),
                   1.0, 0.01, "fresh instance source type is Kick");

        // Every factory value sits inside its parameter's range.
        for (int i = 0; i < bank.getNumPresets(); ++i)
            for (const auto& [id, value] : bank.getPreset (i).values)
            {
                auto* param = dynamic_cast<juce::RangedAudioParameter*> (
                                  p.getAPVTS().getParameter (id));
                check (param != nullptr
                        && value >= param->getNormalisableRange().start - 1e-4f
                        && value <= param->getNormalisableRange().end + 1e-4f,
                       bank.getPreset (i).name + " " + id + " in range");
            }

        // Loading applies values; excluded params stay put.
        setParam (p, pid::inputGain, -5.0f);
        int deep808 = -1;
        for (int i = 0; i < bank.getNumPresets(); ++i)
            if (bank.getPreset (i).name == "Deep 808 Control")
                deep808 = i;
        check (deep808 >= 0, "Deep 808 Control exists");
        bank.load (deep808);
        checkNear ((double) p.getAPVTS().getRawParameterValue (pid::sourceType)->load(),
                   4.0, 0.01, "preset sets source type 808");
        checkNear ((double) p.getAPVTS().getRawParameterValue (pid::body)->load(),
                   70.0, 0.1, "preset sets Body 70");
        checkNear ((double) p.getAPVTS().getRawParameterValue (pid::inputGain)->load(),
                   -5.0, 0.01, "preset load leaves the input trim alone");
        check (! bank.isModified(), "freshly loaded preset is clean");

        // Modified tracking by snapshot.
        setParam (p, pid::punch, 90.0f);
        check (bank.isModified(), "tweaking a macro marks the preset modified");
        bank.load (deep808);
        check (! bank.isModified(), "reloading clears the modified flag");

        // Undoable loads.
        bank.load (0);
        check (bank.getCurrentName() == "Punchy Kick Starter", "loaded preset 0");
        check (p.getUndoManager().canUndo(), "preset load is undoable");
        p.getUndoManager().undo();
        check (bank.getCurrentName() == "Deep 808 Control",
               "undo returns to the previous preset (got '" + bank.getCurrentName() + "')");
        checkNear ((double) p.getAPVTS().getRawParameterValue (pid::body)->load(),
                   70.0, 0.1, "undo restores the previous values");

        // Prev/next wrap.
        bank.load (bank.getNumPresets() - 1);
        bank.step (1);
        check (bank.getCurrentIndex() == 0, "next wraps to the first preset");
        bank.step (-1);
        check (bank.getCurrentIndex() == bank.getNumPresets() - 1,
               "previous wraps to the last preset");

        // User presets: save, then a new instance sees and loads it.
        setParam (p, pid::punch, 77.0f);
        setParam (p, pid::saturate, 61.0f);
        check (bank.saveUserPreset ("My Test Kick"), "user preset saves");
        check (bank.getCurrentName() == "My Test Kick", "saved preset becomes current");
        check (! bank.isModified(), "saved preset is clean");
        check (! bank.currentIsFactory(), "saved preset is a user preset");
    }
    {
        SourceGloProcessor p2;
        auto& bank2 = p2.getPresets();
        int mine = -1;
        for (int i = 0; i < bank2.getNumPresets(); ++i)
            if (bank2.getPreset (i).name == "My Test Kick")
                mine = i;
        check (mine >= 0, "user preset appears in a new instance");
        if (mine >= 0)
        {
            bank2.load (mine);
            checkNear ((double) p2.getAPVTS().getRawParameterValue (pid::punch)->load(),
                       77.0, 0.1, "user preset round-trips Punch");
            checkNear ((double) p2.getAPVTS().getRawParameterValue (pid::saturate)->load(),
                       61.0, 0.1, "user preset round-trips Saturate");
        }
    }

    // ------------------------------------------------------------------ state
    std::printf ("- state round-trip\n");
    {
        SourceGloProcessor a;
        setParam (a, pid::punch, 83.0f);
        setParam (a, pid::sourceType, 4.0f);
        a.setSavedUIScale (1.25f);
        a.getPresets().load (2);
        // Re-stage the values the assertions check - the preset load above
        // rewrote the creative parameters.
        setParam (a, pid::punch, 83.0f);
        setParam (a, pid::sourceType, 4.0f);

        juce::MemoryBlock blob;
        a.getStateInformation (blob);

        SourceGloProcessor b;
        b.setStateInformation (blob.getData(), (int) blob.getSize());

        checkNear ((double) b.getAPVTS().getRawParameterValue (pid::punch)->load(), 83.0, 0.01,
                   "punch survives round-trip");
        checkNear ((double) b.getAPVTS().getRawParameterValue (pid::sourceType)->load(), 4.0, 0.01,
                   "source type survives round-trip");
        checkNear ((double) b.getSavedUIScale(), 1.25, 0.001, "UI scale survives round-trip");
        check (b.getPresets().getCurrentName() == a.getPresets().getCurrentName(),
               "preset name survives round-trip (got '" + b.getPresets().getCurrentName() + "')");
    }

    // ----------------------------------------------------------------- editor
    std::printf ("- editor\n");
    {
        SourceGloProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        for (int i = 0; i < 25; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            check (ed != nullptr, "editor creates");
            ed->setSize (Design::width, Design::height);
        }

        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (Design::width, Design::height);

        // Aspect lock: constrainer enforces 1491:1055.
        check (ed->getConstrainer() != nullptr
                && std::abs (ed->getConstrainer()->getFixedAspectRatio()
                             - (double) Design::aspect) < 1e-3,
               "aspect ratio locked");

        // Accessibility: every interactive component carries a title/tooltip.
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            if (dynamic_cast<juce::Button*> (&c) != nullptr
                 || dynamic_cast<juce::Slider*> (&c) != nullptr)
                check (c.getTitle().isNotEmpty() || c.getName().isNotEmpty(),
                       "control has accessible name: " + c.getTitle() + c.getName());
            for (auto* child : c.getChildren())
                walk (*child);
        };
        walk (*ed);
    }

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
