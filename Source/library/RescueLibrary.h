/*
    RescueLibrary.h - the local sample library behind the rescue rows.

    A background thread scans the user's folders, runs a light analysis on
    each audio file (band balance, peak, crest, duration, a 24-bar waveform
    overview) and keeps the results in a JSON index under
    ~/Library/Application Support/Diamond Loopz/SourceGlo Pro/. Matching
    ranks the index against the selected source type's target profile - the
    suggestion is "a sample that IS what your source is supposed to be".

    Local files only; nothing leaves the machine (the pack forbids cloud
    dependencies). All scanning happens off the audio and message threads;
    the matcher runs on the message thread over a locked copy.
*/

#pragma once
#include <JuceHeader.h>
#include "../AnalysisModel.h"
#include "../dsp/AnalysisEngine.h"

namespace sourceglo
{

struct LibraryEntry
{
    juce::String path;
    juce::int64 modificationTime = 0;
    float durationSec = 0.0f;
    float peakDb = -120.0f, crestDb = 0.0f;
    float bandLevelDb[5] { 0, 0, 0, 0, 0 };    // re loudest band
    std::array<float, RescueSuggestion::waveformBars> waveform {};
    bool favourite = false;
};

class RescueLibrary : private juce::Thread
{
public:
    RescueLibrary();
    ~RescueLibrary() override;

    // --- folders + scanning (message thread) ------------------------------
    juce::StringArray getFolders() const;
    void addFolder (const juce::File& folder);
    void removeFolder (const juce::File& folder);
    void rescan();

    bool isScanning() const                     { return isThreadRunning(); }
    int getIndexedCount() const;
    int getScanProgress() const                 { return scanProgress.load(); }   // files handled this pass

    void setFavourite (const juce::String& path, bool fav);

    // --- matching (message thread) ----------------------------------------
    // Top `count` suggestions for a source type; deterministic order.
    std::vector<RescueSuggestion> match (int sourceTypeIndex, int count = 5) const;

    // Fired on scan progress/completion and favourite changes.
    juce::ChangeBroadcaster changed;

    // Light per-file analysis - public and static so the tests can pin it.
    static bool analyseFile (const juce::File& file, juce::AudioFormatManager& formats,
                             LibraryEntry& out);

    static juce::File indexFile();

    // Tests point this somewhere disposable so they never touch the user's
    // real index. Set before constructing any RescueLibrary (or processor).
    static juce::File& indexFileOverride();

private:
    void run() override;
    void load();
    void save() const;

    juce::AudioFormatManager formats;

    mutable juce::CriticalSection lock;
    juce::StringArray folders;
    std::vector<LibraryEntry> entries;

    std::atomic<int> scanProgress { 0 };
};

} // namespace sourceglo
