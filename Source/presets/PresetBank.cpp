#include "PresetBank.h"
#include "../PluginProcessor.h"

namespace sourceglo
{

const juce::StringArray& PresetBank::presetParameterIds()
{
    static const juce::StringArray ids {
        pid::sourceType, pid::fixAmount, pid::punch, pid::body, pid::tone,
        pid::air, pid::stereo, pid::transients, pid::saturate, pid::sub
    };
    return ids;
}

// -----------------------------------------------------------------------------
//  Factory bank. "Punchy Kick Starter" equals the parameter defaults plus
//  source type Kick, so a fresh instance opens on it unmodified - exactly the
//  approved mockup's header.
// -----------------------------------------------------------------------------
void PresetBank::buildFactoryBank()
{
    struct Row { const char* name; const char* cat; float type, fix, punch,
                 body, tone, air, stereo, trans, sat, sub; };

    static const Row rows[] = {
        { "Punchy Kick Starter",  "Kick",   1, 50, 60, 55, 50, 40, 20, 65, 35, 20 },
        { "Deep Kick Foundation", "Kick",   1, 50, 45, 75, 42, 20,  0, 40, 30, 35 },
        { "Vintage Kick Warmth",  "Kick",   1, 40, 35, 65, 38, 15, 10, 30, 60, 25 },
        { "Tight Kick Snap",      "Kick",   1, 55, 80, 35, 55, 45, 10, 80, 25, 15 },
        { "Deep 808 Control",     "808",    4, 60, 30, 70, 40, 10,  0, 25, 45, 45 },
        { "Clean Sub 808",        "808",    4, 55, 20, 60, 35,  5,  0, 15, 15, 50 },
        { "808 Glue & Grit",      "808",    4, 50, 40, 55, 45, 15,  5, 35, 70, 40 },
        { "Aggressive 808 Bite",  "808",    4, 55, 55, 45, 60, 30, 10, 60, 80, 35 },
        { "Snare Snap Doctor",    "Snare",  2, 50, 70, 40, 55, 55, 25, 75, 30,  0 },
        { "Fat Snare Body",       "Snare",  2, 45, 50, 75, 45, 35, 20, 45, 45,  5 },
        { "Crisp Snare Air",      "Snare",  2, 45, 55, 30, 60, 75, 30, 60, 20,  0 },
        { "Clap Sheen",           "Clap",   3, 40, 45, 25, 58, 70, 40, 55, 25,  0 },
        { "Wide Clap Room",       "Clap",   3, 40, 35, 35, 52, 55, 70, 40, 30,  0 },
        { "Bass Focus Clean",     "Bass",   5, 55, 35, 60, 45, 10,  0, 30, 25, 30 },
        { "Warm Bass Body",       "Bass",   5, 50, 25, 75, 40,  8,  0, 20, 50, 40 },
        { "Hat Polish",           "Hat",    6, 40, 40, 10, 62, 70, 35, 55, 15,  0 },
        { "Crisp Hat Air",        "Hat",    6, 45, 50,  5, 68, 85, 45, 65, 10,  0 },
        { "Perc Presence",        "Percussion", 7, 45, 55, 30, 58, 55, 45, 60, 25,  0 },
        { "Loop Glue Fast",       "Loop",   8, 50, 45, 50, 50, 40, 30, 50, 40, 15 },
        { "Loop Tape Warmth",     "Loop",   8, 45, 30, 65, 42, 25, 25, 35, 65, 20 },
        { "Loop Wide & Bright",   "Loop",   8, 45, 40, 40, 60, 65, 75, 45, 30, 10 },
        { "Melody Space",         "Melody", 9, 45, 30, 45, 52, 55, 65, 35, 25,  0 },
        { "Melody Presence",      "Melody", 9, 50, 45, 50, 58, 60, 35, 55, 35,  5 },
        { "Vocal Clarity Rescue", "Vocal", 10, 60, 40, 35, 60, 65, 15, 50, 20,  0 },
        { "Vocal Warm Body",      "Vocal", 10, 50, 30, 70, 45, 40, 10, 35, 40,  0 },
        { "Vocal Air & Sheen",    "Vocal", 10, 45, 35, 25, 58, 85, 25, 45, 15,  0 },
        { "Flat Start",           "Utility", 0, 50,  0,  0, 50,  0,  0,  0,  0,  0 },
        { "Gentle Enhance",       "Utility", 0, 40, 30, 30, 52, 30, 15, 30, 20, 10 },
        { "Maximum Character",    "Utility", 0, 70, 75, 65, 58, 65, 40, 80, 70, 30 },
    };

    for (const auto& r : rows)
    {
        Preset p;
        p.name = r.name;
        p.category = r.cat;
        p.factory = true;
        p.values = {
            { pid::sourceType, r.type },  { pid::fixAmount, r.fix },
            { pid::punch, r.punch },      { pid::body, r.body },
            { pid::tone, r.tone },        { pid::air, r.air },
            { pid::stereo, r.stereo },    { pid::transients, r.trans },
            { pid::saturate, r.sat },     { pid::sub, r.sub },
        };
        presets.push_back (std::move (p));
    }
}

// -----------------------------------------------------------------------------
PresetBank::PresetBank (juce::AudioProcessorValueTreeState& state, juce::UndoManager& undo)
    : apvts (state), undoManager (undo)
{
    buildFactoryBank();
    scanUserPresets();
    load (0, false);           // fresh instance = the mockup's header preset
}

juce::File& PresetBank::userDirOverride()
{
    static juce::File overrideDir;
    return overrideDir;
}

juce::File PresetBank::userPresetDirectory()
{
    if (userDirOverride() != juce::File())
        return userDirOverride();
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Application Support/Diamond Loopz/SourceGlo Pro/Presets/User");
}

void PresetBank::scanUserPresets()
{
    presets.erase (std::remove_if (presets.begin(), presets.end(),
                      [] (const Preset& p) { return ! p.factory; }),
                   presets.end());

    for (const auto& item : juce::RangedDirectoryIterator (
             userPresetDirectory(), false, "*.sourceglopreset", juce::File::findFiles))
    {
        const auto json = juce::JSON::parse (item.getFile().loadFileAsString());
        Preset p;
        p.name = json["name"].toString();
        if (p.name.isEmpty())
            continue;
        p.category = "User";
        p.factory = false;
        p.file = item.getFile();
        for (const auto& id : presetParameterIds())
            if (json["values"].hasProperty (juce::Identifier (id)))
                p.values[id] = (float) (double) json["values"][juce::Identifier (id)];
        presets.push_back (std::move (p));
    }

    std::stable_sort (presets.begin(), presets.end(), [] (const Preset& a, const Preset& b)
    {
        if (a.factory != b.factory) return a.factory;
        if (! a.factory) return a.name.compareIgnoreCase (b.name) < 0;
        return false;                       // factory keeps table order
    });
}

// -----------------------------------------------------------------------------
//  Values + snapshot
// -----------------------------------------------------------------------------
std::map<juce::String, float> PresetBank::currentValues() const
{
    std::map<juce::String, float> out;
    for (const auto& id : presetParameterIds())
        if (auto* p = apvts.getParameter (id))
            out[id] = dynamic_cast<juce::RangedAudioParameter*> (p)
                        ->convertFrom0to1 (p->getValue());
    return out;
}

void PresetBank::applyValues (const std::map<juce::String, float>& values)
{
    for (const auto& [id, value] : values)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (
                dynamic_cast<juce::RangedAudioParameter*> (p)->convertTo0to1 (value));
}

void PresetBank::takeSnapshot()
{
    snapshot = currentValues();
}

bool PresetBank::isModified() const
{
    if (snapshot.empty())
        return false;
    const auto now = currentValues();
    for (const auto& [id, value] : snapshot)
    {
        const auto it = now.find (id);
        if (it != now.end() && std::abs (it->second - value) > 0.05f)
            return true;
    }
    return false;
}

juce::String PresetBank::getCurrentName() const
{
    return currentIndex >= 0 && currentIndex < getNumPresets()
             ? presets[(size_t) currentIndex].name : restoredName;
}

bool PresetBank::currentIsFactory() const
{
    return currentIndex >= 0 && currentIndex < getNumPresets()
             && presets[(size_t) currentIndex].factory;
}

// -----------------------------------------------------------------------------
//  Loading (undoable)
// -----------------------------------------------------------------------------
class PresetLoadAction : public juce::UndoableAction
{
public:
    PresetLoadAction (PresetBank& bankIn, int newIndexIn,
                      std::map<juce::String, float> oldValuesIn, int oldIndexIn)
        : bank (bankIn), newIndex (newIndexIn),
          oldValues (std::move (oldValuesIn)), oldIndex (oldIndexIn) {}

    bool perform() override
    {
        bank.applyValues (bank.presets[(size_t) newIndex].values);
        bank.currentIndex = newIndex;
        bank.takeSnapshot();
        bank.changed.sendChangeMessage();
        return true;
    }

    bool undo() override
    {
        bank.applyValues (oldValues);
        bank.currentIndex = oldIndex;
        bank.snapshot = oldValues;
        bank.changed.sendChangeMessage();
        return true;
    }

private:
    PresetBank& bank;
    int newIndex;
    std::map<juce::String, float> oldValues;
    int oldIndex;
};

void PresetBank::load (int index, bool undoable)
{
    index = juce::jlimit (0, getNumPresets() - 1, index);

    if (undoable)
    {
        undoManager.beginNewTransaction ("Load preset " + presets[(size_t) index].name);
        undoManager.perform (new PresetLoadAction (*this, index, currentValues(), currentIndex));
    }
    else
    {
        applyValues (presets[(size_t) index].values);
        currentIndex = index;
        takeSnapshot();
        changed.sendChangeMessage();
    }
}

void PresetBank::step (int delta)
{
    const int n = getNumPresets();
    load (((currentIndex < 0 ? 0 : currentIndex) + delta % n + n) % n);
}

// -----------------------------------------------------------------------------
//  Saving
// -----------------------------------------------------------------------------
bool PresetBank::saveUserPreset (const juce::String& rawName)
{
    const auto name = rawName.trim();
    if (name.isEmpty())
        return false;

    auto* root = new juce::DynamicObject();
    root->setProperty ("name", name);
    auto* vals = new juce::DynamicObject();
    for (const auto& [id, value] : currentValues())
        vals->setProperty (juce::Identifier (id), value);
    root->setProperty ("values", juce::var (vals));

    auto dir = userPresetDirectory();
    dir.createDirectory();
    auto file = dir.getChildFile (juce::File::createLegalFileName (name)
                                    + ".sourceglopreset");
    if (! file.replaceWithText (juce::JSON::toString (juce::var (root))))
        return false;

    scanUserPresets();
    for (int i = 0; i < getNumPresets(); ++i)
        if (! presets[(size_t) i].factory && presets[(size_t) i].name == name)
            currentIndex = i;
    takeSnapshot();
    changed.sendChangeMessage();
    return true;
}

bool PresetBank::overwriteCurrent()
{
    if (currentIsFactory() || currentIndex < 0)
        return false;
    return saveUserPreset (presets[(size_t) currentIndex].name);
}

void PresetBank::notePresetRestored (const juce::String& name)
{
    currentIndex = -1;
    restoredName = name.isNotEmpty() ? name : "Custom";
    for (int i = 0; i < getNumPresets(); ++i)
        if (presets[(size_t) i].name == name)
            currentIndex = i;
    takeSnapshot();               // restored session starts clean
    changed.sendChangeMessage();
}

} // namespace sourceglo
