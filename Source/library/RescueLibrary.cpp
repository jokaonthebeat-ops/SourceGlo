#include "RescueLibrary.h"

namespace sourceglo
{

RescueLibrary::RescueLibrary() : juce::Thread ("SourceGlo Library Scan")
{
    formats.registerBasicFormats();
    load();
}

RescueLibrary::~RescueLibrary()
{
    stopThread (4000);
}

juce::File& RescueLibrary::indexFileOverride()
{
    static juce::File overrideFile;
    return overrideFile;
}

juce::File RescueLibrary::indexFile()
{
    if (indexFileOverride() != juce::File())
        return indexFileOverride();

    // NOTE: userApplicationDataDirectory is ~/Library on macOS, not
    // ~/Library/Application Support.
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Application Support/Diamond Loopz/SourceGlo Pro/LibraryIndex.json");
}

// -----------------------------------------------------------------------------
//  Folders + scanning
// -----------------------------------------------------------------------------
juce::StringArray RescueLibrary::getFolders() const
{
    const juce::ScopedLock sl (lock);
    return folders;
}

int RescueLibrary::getIndexedCount() const
{
    const juce::ScopedLock sl (lock);
    return (int) entries.size();
}

void RescueLibrary::addFolder (const juce::File& folder)
{
    if (! folder.isDirectory())
        return;
    {
        const juce::ScopedLock sl (lock);
        if (folders.contains (folder.getFullPathName()))
            return;
        folders.add (folder.getFullPathName());
    }
    rescan();
}

void RescueLibrary::removeFolder (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (lock);
        folders.removeString (folder.getFullPathName());
        entries.erase (std::remove_if (entries.begin(), entries.end(),
                          [&] (const LibraryEntry& e)
                          { return juce::File (e.path).isAChildOf (folder); }),
                       entries.end());
    }
    save();
    changed.sendChangeMessage();
}

void RescueLibrary::rescan()
{
    if (! isThreadRunning())
    {
        scanProgress.store (0);
        startThread (juce::Thread::Priority::low);
    }
}

void RescueLibrary::run()
{
    juce::StringArray toScan;
    std::vector<LibraryEntry> existing;
    {
        const juce::ScopedLock sl (lock);
        toScan = folders;
        existing = entries;
    }

    auto findExisting = [&existing] (const juce::String& path) -> const LibraryEntry*
    {
        for (const auto& e : existing)
            if (e.path == path)
                return &e;
        return nullptr;
    };

    std::vector<LibraryEntry> fresh;
    int handled = 0;

    for (const auto& folderPath : toScan)
    {
        for (const auto& item : juce::RangedDirectoryIterator (
                 juce::File (folderPath), true, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a",
                 juce::File::findFiles))
        {
            if (threadShouldExit())
                return;

            const auto file = item.getFile();
            const auto mtime = file.getLastModificationTime().toMilliseconds();

            if (const auto* known = findExisting (file.getFullPathName());
                known != nullptr && known->modificationTime == mtime)
            {
                fresh.push_back (*known);            // unchanged: keep as-is
            }
            else
            {
                LibraryEntry entry;
                if (analyseFile (file, formats, entry))
                {
                    entry.modificationTime = mtime;
                    if (known != nullptr)
                        entry.favourite = known->favourite;
                    fresh.push_back (std::move (entry));
                }
            }

            scanProgress.store (++handled);
            if (handled % 16 == 0)
            {
                const juce::ScopedLock sl (lock);
                entries = fresh;
                changed.sendChangeMessage();
            }
        }
    }

    {
        const juce::ScopedLock sl (lock);
        entries = std::move (fresh);
    }
    save();
    changed.sendChangeMessage();
}

// -----------------------------------------------------------------------------
//  Per-file light analysis: peak / crest / duration / band balance / overview.
// -----------------------------------------------------------------------------
bool RescueLibrary::analyseFile (const juce::File& file, juce::AudioFormatManager& fm,
                                 LibraryEntry& out)
{
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 256)
        return false;

    out.path = file.getFullPathName();
    out.durationSec = (float) (reader->lengthInSamples / reader->sampleRate);

    // Read up to 8 s for measurement, mono-summed.
    const int n = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                 (juce::int64) (reader->sampleRate * 8.0));
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, n);
    reader->read (&buffer, 0, n, 0, true, true);

    std::vector<float> mono ((size_t) n, 0.0f);
    for (int ch = 0; ch < (int) reader->numChannels; ++ch)
    {
        const float* src = buffer.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
            mono[(size_t) i] += src[i] / (float) reader->numChannels;
    }

    double sumSq = 0.0;
    float peak = 0.0f;
    for (float v : mono)
    {
        peak = juce::jmax (peak, std::abs (v));
        sumSq += (double) v * v;
    }
    out.peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
    out.crestDb = out.peakDb
                    - juce::Decibels::gainToDecibels ((float) std::sqrt (sumSq / n), -120.0f);

    // Waveform overview: per-bar peak, normalised.
    {
        const int bars = RescueSuggestion::waveformBars;
        const int per = juce::jmax (1, n / bars);
        for (int b = 0; b < bars; ++b)
        {
            float m = 0.0f;
            for (int i = b * per; i < juce::jmin (n, (b + 1) * per); ++i)
                m = juce::jmax (m, std::abs (mono[(size_t) i]));
            out.waveform[(size_t) b] = peak > 1.0e-6f ? m / peak : 0.0f;
        }
    }

    // Band balance (Welch, 4096 Hann, hop 2048; zero-pad short files).
    {
        constexpr int order = 12, size = 1 << order, hop = size / 2;
        juce::dsp::FFT fft (order);
        std::vector<float> window ((size_t) size);
        juce::dsp::WindowingFunction<float>::fillWindowingTables (
            window.data(), (size_t) size, juce::dsp::WindowingFunction<float>::hann);
        std::vector<float> frame ((size_t) size * 2, 0.0f);
        std::vector<double> power ((size_t) size / 2, 0.0);

        int frames = 0;
        for (int start = 0; start == 0 || start + size <= n; start += hop, ++frames)
        {
            std::fill (frame.begin(), frame.end(), 0.0f);
            for (int i = 0; i < size && start + i < n; ++i)
                frame[(size_t) i] = mono[(size_t) (start + i)] * window[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (frame.data());
            for (int k = 0; k < size / 2; ++k)
                power[(size_t) k] += (double) frame[(size_t) k] * frame[(size_t) k];
            if (start + size > n)
                break;
        }

        const float* edges = AnalysisEngine::bandEdgesHz();
        double bandPower[5] = {};
        for (int k = 1; k < size / 2; ++k)
        {
            const double hz = k * reader->sampleRate / size;
            for (int b = 0; b < 5; ++b)
                if (hz >= edges[b] && hz < edges[b + 1])
                {
                    bandPower[b] += power[(size_t) k] / juce::jmax (1, frames);
                    break;
                }
        }

        float loudest = -300.0f;
        float bandDb[5];
        for (int b = 0; b < 5; ++b)
        {
            bandDb[b] = bandPower[b] > 0.0 ? 10.0f * (float) std::log10 (bandPower[b]) : -300.0f;
            loudest = juce::jmax (loudest, bandDb[b]);
        }
        for (int b = 0; b < 5; ++b)
            out.bandLevelDb[b] = juce::jmax (-60.0f, bandDb[b] - loudest);
    }

    return true;
}

// -----------------------------------------------------------------------------
//  Matching
// -----------------------------------------------------------------------------
static void describeEntry (const LibraryEntry& e, juce::String& tagA, juce::String& tagB)
{
    // Tone character from the dominant band...
    int dominant = 0;
    for (int b = 1; b < 5; ++b)
        if (e.bandLevelDb[b] > e.bandLevelDb[dominant])
            dominant = b;
    static const char* toneTags[5] = { "Deep", "Warm", "Full", "Bright", "Crisp" };
    tagA = toneTags[dominant];

    // ...and dynamics/length for the second word.
    if (e.crestDb > 12.0f)            tagB = "Punchy";
    else if (e.crestDb < 6.0f)        tagB = "Dense";
    else if (e.durationSec < 0.6f)    tagB = "Tight";
    else if (e.durationSec > 2.0f)    tagB = "Extended";
    else                              tagB = "Solid";
}

std::vector<RescueSuggestion> RescueLibrary::match (int typeIndex, int count) const
{
    std::vector<LibraryEntry> snapshot;
    {
        const juce::ScopedLock sl (lock);
        snapshot = entries;
    }

    struct Scored { float score; const LibraryEntry* entry; };
    std::vector<Scored> scored;
    scored.reserve (snapshot.size());

    for (const auto& e : snapshot)
    {
        // How closely this file matches what the source is SUPPOSED to be.
        // Asymmetric: in the profile's prominent bands, missing energy and
        // excess both hurt; in its background bands only EXCESS hurts -
        // being even quieter than an already-quiet target is fine. Without
        // this, sparse one-shots (a pure 808 sine has -60 dB mids) rack up
        // huge "deviations" everywhere and every candidate saturates to the
        // same floor score, degenerating the ranking to alphabetical order.
        float weightedPenalty = 0.0f, weightSum = 0.0f;
        for (int b = 0; b < 5; ++b)
        {
            const float target = AnalysisEngine::targetBandDb (typeIndex, b);
            const float dev = e.bandLevelDb[b] - target;
            const bool prominent = target >= -6.0f;
            const float penalty = juce::jmin (20.0f,
                prominent ? std::abs (dev) : juce::jmax (0.0f, dev));
            const float weight = prominent ? 1.0f : 0.7f;
            weightedPenalty += penalty * weight;
            weightSum += weight;
        }
        float score = 100.0f - (weightedPenalty / weightSum) * 4.5f;

        // Duration suitability: one-shot types want one-shots.
        const bool oneShotType = typeIndex == 1 || typeIndex == 2 || typeIndex == 3
                              || typeIndex == 6 || typeIndex == 7;
        if (oneShotType && e.durationSec > 2.0f)  score -= 10.0f;
        if (typeIndex == 8 && e.durationSec < 1.0f) score -= 10.0f;   // loops want length

        if (e.favourite)
            score += 4.0f;

        scored.push_back ({ juce::jlimit (1.0f, 99.0f, score), &e });
    }

    std::sort (scored.begin(), scored.end(), [] (const Scored& a, const Scored& b)
    {
        if (a.score != b.score) return a.score > b.score;
        return a.entry->path < b.entry->path;      // deterministic ties
    });

    std::vector<RescueSuggestion> result;
    for (int i = 0; i < (int) scored.size() && i < count; ++i)
    {
        const auto& e = *scored[(size_t) i].entry;
        RescueSuggestion s;
        s.path = e.path;
        s.fileName = juce::File (e.path).getFileName();
        describeEntry (e, s.tagA, s.tagB);
        s.fitPercent = (int) std::lround (scored[(size_t) i].score);
        s.favourite = e.favourite;
        s.waveform = e.waveform;
        result.push_back (std::move (s));
    }
    return result;
}

void RescueLibrary::setFavourite (const juce::String& path, bool fav)
{
    {
        const juce::ScopedLock sl (lock);
        for (auto& e : entries)
            if (e.path == path)
                e.favourite = fav;
    }
    save();
    changed.sendChangeMessage();
}

// -----------------------------------------------------------------------------
//  Persistence
// -----------------------------------------------------------------------------
void RescueLibrary::load()
{
    const auto file = indexFile();
    if (! file.existsAsFile())
        return;

    const auto json = juce::JSON::parse (file.loadFileAsString());
    const juce::ScopedLock sl (lock);

    folders.clear();
    if (auto* list = json["folders"].getArray())
        for (const auto& f : *list)
            folders.add (f.toString());

    entries.clear();
    if (auto* list = json["entries"].getArray())
        for (const auto& v : *list)
        {
            LibraryEntry e;
            e.path = v["path"].toString();
            e.modificationTime = (juce::int64) v["mtime"];
            e.durationSec = (float) (double) v["dur"];
            e.peakDb = (float) (double) v["peak"];
            e.crestDb = (float) (double) v["crest"];
            e.favourite = (bool) v["fav"];
            if (auto* bands = v["bands"].getArray())
                for (int b = 0; b < 5 && b < bands->size(); ++b)
                    e.bandLevelDb[b] = (float) (double) (*bands)[b];
            if (auto* wave = v["wave"].getArray())
                for (int b = 0; b < (int) e.waveform.size() && b < wave->size(); ++b)
                    e.waveform[(size_t) b] = (float) (double) (*wave)[b];
            entries.push_back (std::move (e));
        }
}

void RescueLibrary::save() const
{
    juce::StringArray foldersCopy;
    std::vector<LibraryEntry> entriesCopy;
    {
        const juce::ScopedLock sl (lock);
        foldersCopy = folders;
        entriesCopy = entries;
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);

    juce::Array<juce::var> folderList;
    for (const auto& f : foldersCopy)
        folderList.add (f);
    root->setProperty ("folders", folderList);

    juce::Array<juce::var> entryList;
    for (const auto& e : entriesCopy)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("path", e.path);
        o->setProperty ("mtime", e.modificationTime);
        o->setProperty ("dur", e.durationSec);
        o->setProperty ("peak", e.peakDb);
        o->setProperty ("crest", e.crestDb);
        o->setProperty ("fav", e.favourite);
        juce::Array<juce::var> bands;
        for (float b : e.bandLevelDb) bands.add (b);
        o->setProperty ("bands", bands);
        juce::Array<juce::var> wave;
        for (float w : e.waveform) wave.add (w);
        o->setProperty ("wave", wave);
        entryList.add (juce::var (o));
    }
    root->setProperty ("entries", entryList);

    const auto file = indexFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (root)));
}

} // namespace sourceglo
