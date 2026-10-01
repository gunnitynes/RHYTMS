#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace rhytms::state
{

// Factory and user presets as XML files under
//   <user application data>/gunnitynes/RHYTMS/Presets/{Factory,User}
// A preset is the full plugin state: parameters, pinned steps and knob locks.
class PresetManager
{
public:
    static constexpr const char* manufacturerFolder = "gunnitynes";
    static constexpr const char* productFolder = "RHYTMS";

    struct Entry
    {
        juce::String name;
        juce::File file;
        bool factory = false;
    };

    PresetManager();

    juce::File getRootDirectory() const;
    juce::File getUserDirectory() const;
    juce::File getFactoryDirectory() const;

    // Writes any factory preset that is not on disk yet.
    void seedFactory (const std::vector<std::pair<juce::String, juce::ValueTree>>& presets);

    bool save (const juce::ValueTree& state, juce::File target) const;
    juce::ValueTree load (const juce::File& file, const juce::Identifier& expectedType) const;

    // Factory first, then user, each sorted by name.
    std::vector<Entry> getEntries() const;
    int indexOf (const juce::String& name) const;

private:
    static juce::String sanitise (juce::String name);
};

} // namespace rhytms::state
