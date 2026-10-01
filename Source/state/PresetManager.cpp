#include "PresetManager.h"

namespace rhytms::state
{

PresetManager::PresetManager()
{
    getUserDirectory().createDirectory();
    getFactoryDirectory().createDirectory();
}

juce::File PresetManager::getRootDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (manufacturerFolder)
        .getChildFile (productFolder)
        .getChildFile ("Presets");
}

juce::File PresetManager::getUserDirectory() const { return getRootDirectory().getChildFile ("User"); }
juce::File PresetManager::getFactoryDirectory() const { return getRootDirectory().getChildFile ("Factory"); }

juce::String PresetManager::sanitise (juce::String name)
{
    return name.trim().replaceCharacters ("/\\:*?\"<>|", "---------");
}

void PresetManager::seedFactory (const std::vector<std::pair<juce::String, juce::ValueTree>>& presets)
{
    for (const auto& [name, tree] : presets)
    {
        // rewrite factory presets written by an older version, so they pick
        // up new parameters; user presets are never touched
        const auto file = getFactoryDirectory().getChildFile (sanitise (name) + ".xml");
        bool current = false;
        if (file.existsAsFile())
            if (auto xml = juce::XmlDocument::parse (file))
                current = xml->getStringAttribute ("version") == tree.getProperty ("version").toString();
        if (! current)
            save (tree, file);
    }
}

bool PresetManager::save (const juce::ValueTree& state, juce::File target) const
{
    if (target == juce::File {})
        return false;
    if (target.getFileExtension().isEmpty())
        target = target.withFileExtension (".xml");

    target.getParentDirectory().createDirectory();
    if (auto xml = state.createXml())
        return xml->writeTo (target);
    return false;
}

juce::ValueTree PresetManager::load (const juce::File& file, const juce::Identifier& expectedType) const
{
    if (! file.existsAsFile())
        return {};
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (expectedType.toString()))
        return {};
    return juce::ValueTree::fromXml (*xml);
}

std::vector<PresetManager::Entry> PresetManager::getEntries() const
{
    std::vector<Entry> entries;
    auto collect = [&entries] (const juce::File& dir, bool factory)
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*.xml");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        for (const auto& f : files)
            entries.push_back ({ f.getFileNameWithoutExtension(), f, factory });
    };
    collect (getFactoryDirectory(), true);
    collect (getUserDirectory(), false);
    return entries;
}

int PresetManager::indexOf (const juce::String& name) const
{
    const auto entries = getEntries();
    // a user preset with the same name as a factory one wins
    for (int i = (int) entries.size(); --i >= 0;)
        if (entries[(size_t) i].name == name)
            return i;
    return -1;
}

} // namespace rhytms::state
