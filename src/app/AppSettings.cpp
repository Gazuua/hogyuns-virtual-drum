#include "AppSettings.h"

#include "Json.h"

AppSettings::AppSettings (bool rememberSettings)
    : remember (rememberSettings), settings (new juce::DynamicObject())
{
    if (remember)
        json::loadJsonObject (getFile(), settings);
}

juce::File AppSettings::getFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("HogyunsVirtualDrum")
               .getChildFile ("settings.json");
}

juce::var AppSettings::get (const juce::Identifier& name, const juce::var& fallback) const
{
    const auto& value = settings[name];
    return value.isVoid() ? fallback : value;
}

void AppSettings::set (const juce::Identifier& name, const juce::var& value)
{
    if (auto* object = settings.getDynamicObject(); object != nullptr && object->getProperty (name) != value)
    {
        object->setProperty (name, value);
        save();
    }
}

void AppSettings::setLater (const juce::Identifier& name, const juce::var& value)
{
    if (auto* object = settings.getDynamicObject(); object != nullptr && object->getProperty (name) != value)
    {
        object->setProperty (name, value);
        pending = true;
    }
}

void AppSettings::saveIfPending()
{
    if (pending)
        save();
}

void AppSettings::save()
{
    pending = false;

    if (! remember)
        return;

    const auto file = getFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (settings));
}
