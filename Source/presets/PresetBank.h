/*
    PresetBank.h - factory and user presets.

    A preset covers the CREATIVE parameters: source type, fix amount and the
    seven macros. Gain staging (input/output trims), routing (phase, mono),
    engine quality (HQ, oversampling) and bypass deliberately stay outside -
    switching presets must never jump your levels or your latency.

    Modified state is computed by comparing live values against a snapshot
    taken when the preset loaded - never from parameter listeners, whose
    delivery order made every EQGlo preset look dirty the moment it loaded.

    User presets are one JSON file each under
    ~/Library/Application Support/Diamond Loopz/SourceGlo Pro/Presets/User.
    Preset loads run through the UndoManager, so an accidental switch is one
    Cmd+Z away.
*/

#pragma once
#include <JuceHeader.h>

namespace sourceglo
{

struct Preset
{
    juce::String name;
    juce::String category;
    bool factory = true;
    std::map<juce::String, float> values;   // param id -> plain (unnormalised) value
    juce::File file;                        // user presets only
};

class PresetBank
{
public:
    PresetBank (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undo);

    // The parameter ids a preset covers, in a stable order.
    static const juce::StringArray& presetParameterIds();

    int getNumPresets() const                   { return (int) presets.size(); }
    const Preset& getPreset (int index) const   { return presets[(size_t) juce::jlimit (0, getNumPresets() - 1, index)]; }

    int getCurrentIndex() const                 { return currentIndex; }
    juce::String getCurrentName() const;
    bool currentIsFactory() const;
    bool isModified() const;

    void load (int index, bool undoable = true);
    void step (int delta);                      // prev/next with wrap

    // Save the current parameter values as (or over) a user preset.
    bool saveUserPreset (const juce::String& name);
    bool overwriteCurrent();                    // user presets only

    // Called after host state restore: adopt the stored preset name and
    // re-snapshot so the restored session starts clean.
    void notePresetRestored (const juce::String& name);

    static juce::File userPresetDirectory();
    static juce::File& userDirOverride();       // tests point this at scratch

    juce::ChangeBroadcaster changed;

private:
    friend class PresetLoadAction;

    void buildFactoryBank();
    void scanUserPresets();
    void applyValues (const std::map<juce::String, float>& values);
    void takeSnapshot();
    std::map<juce::String, float> currentValues() const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::UndoManager& undoManager;

    std::vector<Preset> presets;
    int currentIndex = 0;
    juce::String restoredName;
    std::map<juce::String, float> snapshot;
};

} // namespace sourceglo
