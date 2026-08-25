#include "AnalysisEngine.h"
#include "PolyphaseInterpolator.h"

namespace sourceglo
{

// =============================================================================
//  Source-type profiles - the "modern pro standard" each source is scored
//  against. Band targets are dB relative to the loudest band; stat windows
//  are dBFS / dB. Tuned for the hip hop / R&B context this catalogue serves.
// =============================================================================
namespace
{
    constexpr int kNumBands = AnalysisResult::numBands;

    // Sub, Low, LowMid, HighMid, High
    const float kBandEdges[kNumBands + 1] = { 20.0f, 60.0f, 250.0f, 2000.0f, 6000.0f, 16000.0f };

    struct Profile
    {
        const char* name;
        float target[kNumBands];      // dB re loudest band
        float peakLo, peakHi;         // preferred sample-peak window, dBFS
        float crestLo, crestHi;       // preferred crest factor window, dB
        bool  lowEndSource;           // masking / body rules apply
        bool  wideAllowed;            // stereo width expected (vs mono-first)
    };

    const Profile kProfiles[12] =
    {
        // name          Sub    Low   LMid  HMid  High   peak        crest      lowEnd wide
        { "Auto",       { -8,   -3,    0,   -8,  -16 }, -10, -1,     4, 16,     true,  true  },
        { "Kick",       {  0,   -2,  -14,  -20,  -30 },  -8, -1,     7, 14,     true,  false },
        { "Snare",      { -18,  -6,    0,   -4,  -10 },  -8, -1,     8, 16,     false, false },
        { "Clap",       { -30, -12,    0,   -2,   -8 }, -10, -1,     8, 16,     false, true  },
        { "808",        {  0,   -4,  -18,  -26,  -34 },  -8, -1,     4, 10,     true,  false },
        { "Bass",       { -2,    0,  -12,  -24,  -32 }, -10, -2,     3,  9,     true,  false },
        { "Hat",        { -40, -25,  -10,    0,   -2 }, -14, -4,     8, 18,     false, true  },
        { "Percussion", { -25, -10,    0,   -3,   -8 }, -12, -2,     7, 16,     false, true  },
        { "Loop",       { -6,   -3,    0,   -6,  -12 },  -6, -1,     6, 12,     true,  true  },
        { "Melody",     { -14,  -4,    0,   -8,  -16 }, -10, -2,     5, 12,     false, true  },
        { "Vocal",      { -30,  -8,    0,   -6,  -14 },  -8, -2,     6, 14,     false, false },
        { "Other",      { -8,   -3,    0,   -8,  -16 }, -10, -1,     4, 16,     false, true  },
    };

    // The mix context a source has to sit in ("Compared to modern pro
    // standard") - drives the Fit score, the radar target and the band bars.
    const float kMixTarget[kNumBands] = { -4.0f, -2.0f, 0.0f, -4.0f, -9.0f };

    const Profile& profileFor (int typeIndex)
    {
        return kProfiles[juce::jlimit (0, 11, typeIndex)];
    }

    float dbOf (float linear)
    {
        return juce::Decibels::gainToDecibels (linear, -120.0f);
    }

    // dB re loudest band -> radar radius. Display mapping only.
    float radarRadius (float db)
    {
        return juce::jlimit (0.12f, 0.85f, 0.76f + db * (0.60f / 34.0f));
    }
}

const float* AnalysisEngine::bandEdgesHz()  { return kBandEdges; }

float AnalysisEngine::targetBandDb (int typeIndex, int band)
{
    return profileFor (typeIndex).target[juce::jlimit (0, kNumBands - 1, band)];
}

// =============================================================================
//  Analysis
// =============================================================================
AnalysisResult AnalysisEngine::analyse (const juce::AudioBuffer<float>& stereo,
                                        double sr, int typeIndex)
{
    AnalysisResult r;
    const auto& profile = profileFor (typeIndex);

    const int total = stereo.getNumSamples();
    if (total < 1024 || stereo.getNumChannels() < 1)
        return r;

    const float* L = stereo.getReadPointer (0);
    const float* R = stereo.getNumChannels() > 1 ? stereo.getReadPointer (1) : L;

    // ---- trim to the active region (10 ms RMS gate at -55 dBFS) ------------
    // Gated on per-channel energy, NOT a mono sum: perfectly out-of-phase
    // stereo cancels in the sum but is very much not silence - it is exactly
    // the material the phase diagnostics exist for.
    const int win = juce::jmax (1, (int) (sr * 0.010));
    int first = -1, last = -1;
    for (int start = 0; start + win <= total; start += win)
    {
        double sum = 0.0;
        for (int i = start; i < start + win; ++i)
            sum += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
        if (dbOf ((float) std::sqrt (sum / win)) > -55.0f)
        {
            if (first < 0) first = start;
            last = start + win;
        }
    }

    if (first < 0 || last - first < (int) (sr * 0.4))
    {
        r.enoughAudio = false;
        r.diagnostics.push_back ({ "Not Enough Audio",
                                   "Play the source through the plugin,\nthen run Analyze again.",
                                   Severity::medium, "analyze" });
        return r;
    }

    const int n = last - first;
    L += first;  R += first;
    r.enoughAudio = true;
    r.durationSeconds = (float) (n / sr);

    // ---- stats: peak, RMS, crest, true peak, correlation, DC ---------------
    {
        double sumSq = 0.0, sumLR = 0.0, sumLL = 0.0, sumRR = 0.0, dc = 0.0;
        double lowLR = 0.0, lowLL = 0.0, lowRR = 0.0;
        float peak = 0.0f;
        float lpL = 0.0f, lpR = 0.0f;
        const float lowCoeff = 1.0f - std::exp ((float) (-2.0 * juce::MathConstants<double>::pi * 120.0 / sr));

        PolyphasePeakDetector tpL, tpR;
        float truePeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], rt = R[i];
            const float m = 0.5f * (l + rt);
            peak = juce::jmax (peak, std::abs (l), std::abs (rt));
            sumSq += (double) m * m;
            sumLR += (double) l * rt;  sumLL += (double) l * l;  sumRR += (double) rt * rt;
            dc += m;

            lpL += lowCoeff * (l - lpL);
            lpR += lowCoeff * (rt - lpR);
            lowLR += (double) lpL * lpR;  lowLL += (double) lpL * lpL;  lowRR += (double) lpR * lpR;

            truePeak = juce::jmax (truePeak, tpL.peakForSample (l), tpR.peakForSample (rt));
        }

        r.peakDb = dbOf (peak);
        r.rmsDb  = dbOf ((float) std::sqrt (sumSq / n));
        r.crestDb = r.peakDb - r.rmsDb;
        r.truePeakDb = dbOf (truePeak);
        r.correlation = (sumLL > 1e-12 && sumRR > 1e-12)
                          ? (float) (sumLR / std::sqrt (sumLL * sumRR)) : 1.0f;
        r.lowCorrelation = (lowLL > 1e-12 && lowRR > 1e-12)
                          ? (float) (lowLR / std::sqrt (lowLL * lowRR)) : 1.0f;

        r.dcOffset = std::abs (dc / n) > 0.01;
        if (r.dcOffset)
            r.diagnostics.push_back ({ "DC Offset Detected",
                                       "The waveform is not centred.\nHigh-pass at 20 Hz to fix it.",
                                       Severity::medium, "wave" });
    }

    // ---- averaged spectrum (Welch, 4096 Hann, hop 2048, mono sum) ----------
    constexpr int fftOrder = 12, fftSize = 1 << fftOrder, hop = fftSize / 2;
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> window ((size_t) fftSize);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (
        window.data(), (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> frame ((size_t) fftSize * 2, 0.0f);
    std::vector<double> avgPower ((size_t) fftSize / 2, 0.0);

    // Onset envelope (spectral flux) collected in the same pass at hop/2.
    constexpr int fluxOrder = 10, fluxSize = 1 << fluxOrder, fluxHop = fluxSize / 2;
    juce::dsp::FFT fluxFft (fluxOrder);
    std::vector<float> fluxWindow ((size_t) fluxSize);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (
        fluxWindow.data(), (size_t) fluxSize, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> fluxFrame ((size_t) fluxSize * 2, 0.0f);
    std::vector<float> prevMag ((size_t) fluxSize / 2, 0.0f);
    std::vector<float> onset;

    int frames = 0;
    for (int start = 0; start + fftSize <= n; start += hop, ++frames)
    {
        for (int i = 0; i < fftSize; ++i)
            frame[(size_t) i] = 0.5f * (L[start + i] + R[start + i]) * window[(size_t) i];
        std::fill (frame.begin() + fftSize, frame.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (frame.data());

        for (int k = 0; k < fftSize / 2; ++k)
            avgPower[(size_t) k] += (double) frame[(size_t) k] * frame[(size_t) k];
    }
    if (frames == 0)
        frames = 1;

    for (int start = 0; start + fluxSize <= n; start += fluxHop)
    {
        for (int i = 0; i < fluxSize; ++i)
            fluxFrame[(size_t) i] = 0.5f * (L[start + i] + R[start + i]) * fluxWindow[(size_t) i];
        std::fill (fluxFrame.begin() + fluxSize, fluxFrame.end(), 0.0f);
        fluxFft.performFrequencyOnlyForwardTransform (fluxFrame.data());

        float flux = 0.0f;
        for (int k = 1; k < fluxSize / 2; ++k)
        {
            const float mag = fluxFrame[(size_t) k];
            flux += juce::jmax (0.0f, mag - prevMag[(size_t) k]);
            prevMag[(size_t) k] = mag;
        }
        onset.push_back (flux);
    }

    // ---- band levels vs the source-type target -----------------------------
    {
        double bandPower[kNumBands] = {};
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const float hz = (float) (k * sr / fftSize);
            for (int b = 0; b < kNumBands; ++b)
                if (hz >= kBandEdges[b] && hz < kBandEdges[b + 1])
                {
                    bandPower[b] += avgPower[(size_t) k] / frames;
                    break;
                }
        }

        float loudest = -300.0f;
        float bandDb[kNumBands];
        for (int b = 0; b < kNumBands; ++b)
        {
            bandDb[b] = bandPower[b] > 0.0 ? 10.0f * (float) std::log10 (bandPower[b]) : -300.0f;
            loudest = juce::jmax (loudest, bandDb[b]);
        }

        for (int b = 0; b < kNumBands; ++b)
        {
            r.bandLevelDb[b] = juce::jmax (-60.0f, bandDb[b] - loudest);
            r.bandDeviationDb[b] = r.bandLevelDb[b] - profile.target[b];

            const float mixDev = r.bandLevelDb[b] - kMixTarget[b];
            r.bandFit[b] = (int) juce::jlimit (0.0f, 100.0f, 100.0f - std::abs (mixDev) * 6.0f);
            r.radarSource[b] = radarRadius (r.bandLevelDb[b]);
            r.radarTarget[b] = radarRadius (kMixTarget[b]);
        }
    }

    // ---- tempo (onset autocorrelation, 60-180 BPM) -------------------------
    if ((int) onset.size() > 64 && r.durationSeconds >= 2.0f)
    {
        // Detrend against a moving average so sustained loudness does not
        // masquerade as periodicity.
        const int ma = 8;
        std::vector<float> env (onset.size(), 0.0f);
        for (size_t i = 0; i < onset.size(); ++i)
        {
            float sum = 0.0f; int c = 0;
            for (int j = -ma; j <= ma; ++j)
            {
                const int idx = (int) i + j;
                if (idx >= 0 && idx < (int) onset.size()) { sum += onset[(size_t) idx]; ++c; }
            }
            env[i] = juce::jmax (0.0f, onset[i] - sum / (float) c);
        }

        const double frameRate = sr / fluxHop;
        const int minLag = (int) std::floor (frameRate * 60.0 / 180.0);
        const int maxLag = (int) std::ceil  (frameRate * 60.0 / 60.0);

        double energy = 0.0;
        for (float v : env) energy += (double) v * v;

        double bestScore = 0.0; int bestLag = 0;
        if (energy > 1e-9)
        {
            auto acf = [&] (int lag) -> double
            {
                if (lag <= 0 || lag >= (int) env.size()) return 0.0;
                double sum = 0.0;
                for (size_t i = 0; i + (size_t) lag < env.size(); ++i)
                    sum += (double) env[i] * env[i + (size_t) lag];
                return sum / energy;
            };

            for (int lag = minLag; lag <= juce::jmin (maxLag, (int) env.size() - 1); ++lag)
            {
                // Reward lags whose double also correlates - the beat, not a
                // single loud repeat.
                const double s = acf (lag) + 0.4 * acf (lag * 2);
                if (s > bestScore) { bestScore = s; bestLag = lag; }
            }
        }

        if (bestLag > minLag && bestScore > 0.10)
        {
            double bpm = 60.0 * frameRate / bestLag;
            while (bpm < 70.0)  bpm *= 2.0;
            while (bpm > 180.0) bpm *= 0.5;
            r.tempoBpm = (float) bpm;
        }
    }

    // ---- key (chroma + Krumhansl-Schmuckler) -------------------------------
    {
        // Chroma needs its own long FFT: the analysis grid above (~11.7 Hz
        // bins) is coarser than a semitone below ~200 Hz, so a 110 Hz tone
        // smears into G#/A# and never lands on A. 32768 samples give 1.5 Hz
        // bins - two per semitone at the bottom of the range.
        //
        // The lowest strong pitch is usually the tonic for this catalogue's
        // sources (808s, basses, loops), so the bass register counts extra -
        // it is what breaks relative/parallel-key ties like A major vs C#
        // minor, which share every triad note.
        double chroma[12] = {};
        {
            constexpr int keyOrder = 15, keySize = 1 << keyOrder, keyHop = keySize / 2;
            if (n >= keySize)
            {
                juce::dsp::FFT keyFft (keyOrder);
                std::vector<float> keyWindow ((size_t) keySize);
                juce::dsp::WindowingFunction<float>::fillWindowingTables (
                    keyWindow.data(), (size_t) keySize,
                    juce::dsp::WindowingFunction<float>::hann);
                std::vector<float> keyFrame ((size_t) keySize * 2, 0.0f);
                std::vector<double> keyPower ((size_t) keySize / 2, 0.0);

                int keyFrames = 0;
                for (int start = 0; start + keySize <= n; start += keyHop, ++keyFrames)
                {
                    for (int i = 0; i < keySize; ++i)
                        keyFrame[(size_t) i] = 0.5f * (L[start + i] + R[start + i])
                                                 * keyWindow[(size_t) i];
                    std::fill (keyFrame.begin() + keySize, keyFrame.end(), 0.0f);
                    keyFft.performFrequencyOnlyForwardTransform (keyFrame.data());
                    for (int k = 0; k < keySize / 2; ++k)
                        keyPower[(size_t) k] += (double) keyFrame[(size_t) k]
                                                 * keyFrame[(size_t) k];
                }

                for (int k = 1; k < keySize / 2; ++k)
                {
                    const double hz = k * sr / keySize;
                    if (hz < 55.0 || hz > 5000.0)
                        continue;
                    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
                    const int pc = ((int) std::llround (midi) % 12 + 12) % 12;
                    const double bassBoost = hz < 250.0 ? 2.2 : 1.0;
                    chroma[pc] += bassBoost * std::sqrt (keyPower[(size_t) k]
                                                           / juce::jmax (1, keyFrames));
                }
            }
        }

        static const double majorProfile[12] = { 6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88 };
        static const double minorProfile[12] = { 6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17 };
        static const char* noteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

        auto pearson = [] (const double* a, const double* b, int rot)
        {
            double ma = 0, mb = 0;
            for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; }
            ma /= 12; mb /= 12;
            double num = 0, da = 0, db2 = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double xa = a[(i + rot) % 12] - ma, xb = b[i] - mb;
                num += xa * xb; da += xa * xa; db2 += xb * xb;
            }
            return (da > 1e-12 && db2 > 1e-12) ? num / std::sqrt (da * db2) : 0.0;
        };

        double best = -2.0, second = -2.0;
        int bestRoot = 0; bool bestMinor = false;
        for (int root = 0; root < 12; ++root)
            for (int minor = 0; minor < 2; ++minor)
            {
                const double c = pearson (chroma, minor != 0 ? minorProfile : majorProfile, root);
                if (c > best)
                {
                    if (root != bestRoot) second = best;
                    bestRoot = root; bestMinor = minor != 0; best = c;
                }
                else if (c > second && root != bestRoot)
                    second = c;
            }

       #if SOURCEGLO_KEY_DEBUG
        {
            std::printf ("chroma:");
            for (int i = 0; i < 12; ++i) std::printf (" %s=%.3f", noteNames[i], chroma[i]);
            std::printf ("\nbest=%.3f (%s %s) second=%.3f\n",
                         best, noteNames[bestRoot], bestMinor ? "min" : "maj", second);
        }
       #endif
        if (best > 0.55 && best - second > 0.04)
            r.keyName = juce::String (noteNames[bestRoot]) + (bestMinor ? " Minor" : " Major");
    }

    // ---- scores ------------------------------------------------------------
    auto windowScore = [] (float value, float lo, float hi, float slope)
    {
        const float dist = value < lo ? lo - value : (value > hi ? value - hi : 0.0f);
        return juce::jlimit (0.0f, 100.0f, 100.0f - dist * slope);
    };

    // Tone: distance from the source-type band profile.
    {
        float meanDev = 0.0f;
        for (int b = 0; b < kNumBands; ++b)
            meanDev += std::abs (r.bandDeviationDb[b]);
        meanDev /= (float) kNumBands;
        r.tone = (int) juce::jlimit (0.0f, 100.0f, 100.0f - meanDev * 6.0f);
    }

    // Level: sample peak in window, true peak under the ceiling, not too quiet.
    {
        float s = windowScore (r.peakDb, profile.peakLo, profile.peakHi, 6.0f);
        if (r.truePeakDb > -0.3f)
            s -= juce::jmin (40.0f, (r.truePeakDb + 0.3f) * 12.0f + 8.0f);
        if (r.rmsDb < -40.0f)
            s = juce::jmin (s, 30.0f);
        r.level = (int) juce::jlimit (0.0f, 100.0f, s);
    }

    // Punch: crest window + onset contrast (how much the attacks stand out).
    {
        const float crestScore = windowScore (r.crestDb, profile.crestLo, profile.crestHi, 8.0f);

        float contrast = 1.0f;
        if (! onset.empty())
        {
            std::vector<float> sorted (onset);
            std::sort (sorted.begin(), sorted.end());
            const size_t top = juce::jmax ((size_t) 1, sorted.size() / 10);
            double topSum = 0.0, allSum = 0.0;
            for (size_t i = sorted.size() - top; i < sorted.size(); ++i) topSum += sorted[i];
            for (float v : sorted) allSum += v;
            const double topMean = topSum / (double) top;
            const double allMean = allSum / (double) sorted.size();
            contrast = allMean > 1e-9 ? (float) (topMean / allMean) : 1.0f;
        }
        const float attackScore = juce::jlimit (0.0f, 100.0f, (contrast - 1.0f) * (100.0f / 7.0f));

        r.punch = (int) juce::jlimit (0.0f, 100.0f, 0.55f * crestScore + 0.45f * attackScore);
    }

    // Phase: mono compatibility, low end weighted hardest.
    r.phase = (int) juce::jlimit (0.0f, 100.0f,
                 (0.30f + 0.28f * juce::jmax (-1.0f, r.correlation)
                        + 0.42f * juce::jmax (-1.0f, r.lowCorrelation)) * 100.0f);

    // Fit: how the source sits against the mix target curve.
    {
        int sum = 0;
        for (int b = 0; b < kNumBands; ++b) sum += r.bandFit[b];
        r.fit = sum / kNumBands;
    }

    r.score = (int) std::lround (0.24 * r.tone + 0.20 * r.punch + 0.20 * r.level
                               + 0.16 * r.phase + 0.20 * r.fit);

    // ---- diagnostics -------------------------------------------------------
    {
        std::vector<Diagnostic> highs, mediums, goods;

        // Clipping: runs of samples pinned at the rail.
        {
            int clipped = 0, run = 0;
            for (int i = 0; i < n; ++i)
            {
                const bool pinned = std::abs (L[i]) >= 0.9995f || std::abs (R[i]) >= 0.9995f;
                run = pinned ? run + 1 : 0;
                if (run >= 3) ++clipped;
            }
            if (clipped > n / 100000 + 2)
                highs.push_back ({ "Digital Clipping Detected",
                                   "Flat-topped peaks found in the capture.\nReduce level before SourceGlo.",
                                   Severity::high, "alert_wave" });
            else
                goods.push_back ({ "Clipping Clean",
                                   "No digital clipping detected.\nSignal integrity good.",
                                   Severity::good, "check" });
        }

        if (r.truePeakDb > 0.0f)
            highs.push_back ({ "Headroom Too Hot",
                               "True peak is over full scale.\nReduce level or short transients.",
                               Severity::high, "gauge" });
        else if (r.truePeakDb > -0.3f)
            mediums.push_back ({ "Headroom Too Hot",
                                 "Peaks are constraining dynamics.\nReduce level or short transients.",
                                 Severity::medium, "gauge" });

        if (profile.lowEndSource && r.bandDeviationDb[0] > 5.0f)
        {
            const bool kick808 = typeIndex == 1 || typeIndex == 4;
            highs.push_back ({ kick808 ? "Kick / 808 Masking Detected" : "Low-End Buildup",
                               "Strong frequency conflict in\n40-80 Hz region.",
                               Severity::high, "alert_wave" });
            r.conflictLoHz = 40.0f;  r.conflictHiHz = 80.0f;
            r.conflictLabel = kick808 ? "KICK / 808 CONFLICT" : "LOW-END CONFLICT";
        }

        if (profile.lowEndSource && r.bandDeviationDb[1] < -5.0f)
            mediums.push_back ({ "Low-End Body Weak",
                                 "Insufficient energy in\n80-150 Hz range.",
                                 Severity::medium, "wave" });

        if (typeIndex != 4 && typeIndex != 5 && r.bandDeviationDb[2] > 5.0f)
            mediums.push_back ({ "Mud Buildup",
                                 "Excess energy around 250-500 Hz\nis clouding the low mids.",
                                 Severity::medium, "wave" });

        if (r.bandDeviationDb[3] > 5.0f)
            mediums.push_back ({ "Harsh Presence",
                                 "The 2-6 kHz region is elevated.\nIt can fatigue and mask vocals.",
                                 Severity::medium, "alert_wave" });

        if (r.correlation < 0.15f)
            highs.push_back ({ "Phase Cancellation Risk",
                               "Channels partially cancel in mono.\nCheck stereo processing upstream.",
                               Severity::high, "phase" });
        else if (profile.wideAllowed && r.lowCorrelation < 0.5f)
            mediums.push_back ({ "Low End Not Mono",
                                 "Bass below 120 Hz is wide.\nSum it to mono for club systems.",
                                 Severity::medium, "phase" });
        else if (r.phase >= 85)
            goods.push_back ({ "Phase Coherent",
                               "Mono compatibility is solid.\nStereo image is safe.",
                               Severity::good, "phase" });

        if (r.crestDb < profile.crestLo - 1.0f)
            mediums.push_back ({ "Transients Flattened",
                                 "Crest factor is low for this source.\nEase compression or clipping.",
                                 Severity::medium, "gauge" });
        else if (r.crestDb > profile.crestHi + 2.0f)
            mediums.push_back ({ "Peaks Uncontrolled",
                                 "Crest factor is high for this source.\nTame peaks to sit in the mix.",
                                 Severity::medium, "gauge" });

        if (r.level >= 80)
            goods.push_back ({ "Level In Range",
                               "Peak and true peak sit inside\nthe pro target window.",
                               Severity::good, "speaker" });
        if (r.tone >= 80)
            goods.push_back ({ "Tone On Target",
                               juce::String ("Balance matches the ") + profile.name
                                 + "\nreference profile.",
                               Severity::good, "wave" });

        for (auto& d : highs)   if (r.diagnostics.size() < 4) r.diagnostics.push_back (std::move (d));
        for (auto& d : mediums) if (r.diagnostics.size() < 4) r.diagnostics.push_back (std::move (d));
        for (auto& d : goods)   if (r.diagnostics.size() < 4) r.diagnostics.push_back (std::move (d));
    }

    return r;
}

// =============================================================================
//  Reference trace - the source-type target curve, smooth in log-f.
// =============================================================================
float AnalysisEngine::targetCurveDb (int typeIndex, float hz)
{
    const auto& profile = profileFor (typeIndex);

    // Band centres in log space; cosine-interpolate between them.
    float centres[kNumBands];
    for (int b = 0; b < kNumBands; ++b)
        centres[b] = std::sqrt (kBandEdges[b] * kBandEdges[b + 1]);

    hz = juce::jlimit (centres[0], centres[kNumBands - 1], hz);

    for (int b = 0; b < kNumBands - 1; ++b)
        if (hz <= centres[b + 1])
        {
            const float t = (std::log (hz) - std::log (centres[b]))
                          / (std::log (centres[b + 1]) - std::log (centres[b]));
            const float s = 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::pi);
            return profile.target[b] + (profile.target[b + 1] - profile.target[b]) * s;
        }

    return profile.target[kNumBands - 1];
}

} // namespace sourceglo
