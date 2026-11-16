#include "UserSettingsStore.h"

namespace pacemaker {

UserSettingsStore::UserSettingsStore(const juce::String& appName)
    : dir_(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile(appName))
{
    dir_.createDirectory();
}

UserSettings UserSettingsStore::load() const
{
    const auto f = dir_.getChildFile("settings.json");
    Json j;
    if (f.existsAsFile() && Json::parse(f.loadFileAsString().toStdString(), j)) return UserSettings::fromJson(j);
    return UserSettings {};
}

bool UserSettingsStore::save(const UserSettings& s) const
{
    return dir_.getChildFile("settings.json").replaceWithText(juce::String(s.toJson().dump(2)));
}

} // namespace pacemaker
