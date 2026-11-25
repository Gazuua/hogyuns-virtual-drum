#include "Documents.h"

namespace
{

bool loadJsonObject (const juce::File& source, juce::var& root)
{
    juce::var parsed;

    if (! source.existsAsFile() || juce::JSON::parse (source.loadFileAsString(), parsed).failed() || ! parsed.isObject())
        return false;

    root = parsed;
    return true;
}

/** 저장하다 중간에 끊겨도 원래 파일이 망가지지 않도록 임시 파일에 쓴 뒤 바꿔치기한다. */
bool writeTextSafely (const juce::File& target, const juce::String& text)
{
    juce::TemporaryFile temporary (target);

    if (! temporary.getFile().replaceWithText (text))
        return false;

    return temporary.overwriteTargetFileWithTemporary();
}

void setOrRemove (juce::DynamicObject& object, const juce::Identifier& name, const juce::var& value)
{
    if (value.isVoid())
        object.removeProperty (name);
    else
        object.setProperty (name, value);
}

} // namespace

//==============================================================================
bool KitDocument::load (const juce::File& kitFile)
{
    juce::var parsed;

    if (! loadJsonObject (kitFile, parsed))
        return false;

    file = kitFile;
    root = parsed;
    return true;
}

bool KitDocument::save() const
{
    return isLoaded() && writeTextSafely (file, juce::JSON::toString (root) + "\n");
}

void KitDocument::setName (const juce::String& newName)
{
    if (auto* object = root.getDynamicObject())
        object->setProperty ("name", newName);
}

juce::var KitDocument::findPad (const juce::String& padId) const
{
    if (const auto* pads = root["pads"].getArray())
        for (const auto& pad : *pads)
            if (pad["id"].toString() == padId)
                return pad;

    return {};
}

double KitDocument::getPadNumber (const juce::String& padId, const juce::Identifier& field, double fallback) const
{
    const auto value = findPad (padId)[field];
    return (value.isDouble() || value.isInt() || value.isInt64()) ? static_cast<double> (value) : fallback;
}

void KitDocument::setPadNumber (const juce::String& padId, const juce::Identifier& field, double value)
{
    if (auto* pad = findPad (padId).getDynamicObject())
        pad->setProperty (field, value);
}

void KitDocument::setPadName (const juce::String& padId, const juce::String& newName)
{
    if (auto* pad = findPad (padId).getDynamicObject())
        pad->setProperty ("name", newName);
}

juce::var KitDocument::getPadProperty (const juce::String& padId, const juce::Identifier& name) const
{
    return findPad (padId)[name];
}

void KitDocument::setPadProperty (const juce::String& padId, const juce::Identifier& name, const juce::var& value)
{
    if (auto* pad = findPad (padId).getDynamicObject())
        setOrRemove (*pad, name, value);
}

void KitDocument::setProperty (const juce::Identifier& name, const juce::var& value)
{
    if (auto* object = root.getDynamicObject())
        setOrRemove (*object, name, value);
}

juce::StringArray KitDocument::getPadIds() const
{
    juce::StringArray ids;

    if (const auto* pads = root["pads"].getArray())
        for (const auto& pad : *pads)
            ids.add (pad["id"].toString());

    return ids;
}

juce::String KitDocument::addPad (const juce::String& name)
{
    auto* object = root.getDynamicObject();

    if (object == nullptr)
        return {};

    if (! root["pads"].isArray())
        object->setProperty ("pads", juce::Array<juce::var>());

    // 겹치지 않는 id를 고른다: pad_1, pad_2, ...
    const auto existing = getPadIds();
    juce::String id;

    for (int number = 1; id.isEmpty() || existing.contains (id); ++number)
        id = "pad_" + juce::String (number);

    auto* pad = new juce::DynamicObject();
    pad->setProperty ("id", id);
    pad->setProperty ("name", name);
    pad->setProperty ("volume", 1.0);
    pad->setProperty ("pan", 0.0);
    pad->setProperty ("samples", juce::Array<juce::var>());

    root["pads"].getArray()->add (juce::var (pad));
    return id;
}

void KitDocument::removePad (const juce::String& padId)
{
    if (auto* pads = root["pads"].getArray())
        for (int i = pads->size(); --i >= 0;)
            if (pads->getReference (i)["id"].toString() == padId)
                pads->remove (i);
}

juce::StringArray KitDocument::describeSamples (const juce::String& padId) const
{
    const auto pad = findPad (padId);
    juce::StringArray lines;

    const auto fileNames = [] (const juce::var& list)
    {
        juce::StringArray names;

        if (const auto* samples = list.getArray())
            for (const auto& sample : *samples)
                names.add (sample.toString().fromLastOccurrenceOf ("/", false, false));

        return names;
    };

    if (const auto* layers = pad["layers"].getArray())
    {
        for (const auto& layer : *layers)
        {
            const auto names = fileNames (layer["samples"]);
            lines.add ("~" + juce::String (static_cast<double> (layer["upTo"]), 2) + "  (" + juce::String (names.size()) + ")  "
                       + names.joinIntoString (", "));
        }
    }
    else
    {
        const auto names = fileNames (pad["samples"]);

        if (! names.isEmpty())
            lines.add ("(" + juce::String (names.size()) + ")  " + names.joinIntoString (", "));
    }

    return lines;
}

int KitDocument::countLayers (const juce::String& padId) const
{
    const auto pad = findPad (padId);

    if (const auto* layers = pad["layers"].getArray())
        return layers->size();

    return pad["samples"].isArray() ? 1 : 0;
}

int KitDocument::countSamples (const juce::String& padId) const
{
    const auto pad = findPad (padId);
    auto count = 0;

    if (const auto* layers = pad["layers"].getArray())
    {
        for (const auto& layer : *layers)
            if (const auto* samples = layer["samples"].getArray())
                count += samples->size();
    }
    else if (const auto* samples = pad["samples"].getArray())
    {
        count = samples->size();
    }

    return count;
}

juce::String KitDocument::setPadSamples (const juce::String& padId, const juce::Array<juce::File>& files, bool append)
{
    auto* pad = findPad (padId).getDynamicObject();

    if (pad == nullptr)
        return "unknown pad: " + padId;

    if (files.isEmpty())
        return "no files";

    const auto kitFolder = getFolder();
    const auto sampleFolder = kitFolder.getChildFile ("samples").getChildFile (juce::File::createLegalFileName (padId));
    juce::Array<juce::var> paths;

    for (const auto& source : files)
    {
        if (! source.existsAsFile())
            return "file not found: " + source.getFullPathName();

        auto target = source;

        // 킷 폴더 밖의 파일은 킷 안으로 복사해서, 킷 폴더만 옮겨도 소리가 따라가게 한다.
        if (! source.isAChildOf (kitFolder))
        {
            if (! sampleFolder.createDirectory())
                return "could not create folder: " + sampleFolder.getFullPathName();

            target = sampleFolder.getNonexistentChildFile (source.getFileNameWithoutExtension(), source.getFileExtension(), false);

            if (! source.copyFileTo (target))
                return "could not copy: " + source.getFileName();
        }

        paths.add (target.getRelativePathFrom (kitFolder).replaceCharacter ('\\', '/'));
    }

    // 샘플을 직접 고르면 더는 모아 둔 소리 그대로가 아니다.
    pad->removeProperty ("sound");

    if (append)
    {
        // 세기 레이어가 있으면 가장 센 레이어에, 없으면 샘플 목록에 덧붙인다.
        juce::var list;

        if (auto* layers = pad->getProperty ("layers").getArray(); layers != nullptr && ! layers->isEmpty())
            list = layers->getReference (layers->size() - 1)["samples"];
        else
            list = pad->getProperty ("samples");

        if (auto* existing = list.getArray())
        {
            existing->addArray (paths);
            return {};
        }
    }

    // 통째로 바꿀 때는 세기 레이어를 없애고 샘플 목록 하나로 둔다.
    pad->removeProperty ("layers");
    pad->setProperty ("samples", paths);
    return {};
}

juce::Array<KitDocument::PadSound> KitDocument::getPadSounds (const juce::String& padId) const
{
    juce::Array<PadSound> sounds;

    if (const auto* list = findPad (padId)["sounds"].getArray())
        for (const auto& sound : *list)
            sounds.add ({ sound["id"].toString(), sound["name"].toString(), sound["family"].toString() });

    return sounds;
}

int KitDocument::getPadSoundIndex (const juce::String& padId) const
{
    const auto pad = findPad (padId);
    const auto current = pad["sound"].toString();

    if (current.isNotEmpty())
        if (const auto* list = pad["sounds"].getArray())
            for (int i = 0; i < list->size(); ++i)
                if (list->getReference (i)["id"].toString() == current)
                    return i;

    return -1;
}

int KitDocument::selectPadSound (const juce::String& padId, int index, bool withFamily)
{
    const auto apply = [] (juce::DynamicObject& pad, const juce::var& sound)
    {
        pad.setProperty ("sound", sound["id"]);
        pad.setProperty ("layers", sound["layers"].clone());    // 사본을 둔다. 모아 둔 원본은 그대로 남는다
        pad.removeProperty ("samples");
    };

    const auto padVar = findPad (padId);
    auto* pad = padVar.getDynamicObject();
    const auto* list = padVar["sounds"].getArray();

    if (pad == nullptr || list == nullptr || ! juce::isPositiveAndBelow (index, list->size()))
        return 0;

    const auto chosen = list->getReference (index);

    if (! chosen["layers"].isArray())
        return 0;

    apply (*pad, chosen);
    auto changed = 1;

    const auto family = chosen["family"].toString();
    const auto* pads = root["pads"].getArray();

    if (! withFamily || family.isEmpty() || pads == nullptr)
        return changed;

    for (const auto& other : *pads)
    {
        auto* otherPad = other.getDynamicObject();
        const auto* otherList = other["sounds"].getArray();

        if (otherPad == nullptr || otherPad == pad || otherList == nullptr)
            continue;

        for (const auto& sound : *otherList)
        {
            if (sound["family"].toString() != family || ! sound["layers"].isArray())
                continue;

            if (other["sound"].toString() != sound["id"].toString())
            {
                const auto same = sound;    // 아래에서 패드의 항목을 바꾸는 동안 붙들어 둔다
                apply (*otherPad, same);
                ++changed;
            }

            break;
        }
    }

    return changed;
}

//==============================================================================
bool KeymapDocument::load (const juce::File& keymapFile)
{
    juce::var parsed;

    if (! loadJsonObject (keymapFile, parsed))
        return false;

    file = keymapFile;
    root = parsed;
    return true;
}

bool KeymapDocument::save() const
{
    const auto* object = root.getDynamicObject();

    if (object == nullptr)
        return false;

    // 사람이 읽고 고치기 쉽게, 목록(줄 설명과 키 줄들)은 한 줄에 하나씩 적는다.
    juce::String text ("{\n");
    const auto& properties = object->getProperties();

    for (int i = 0; i < properties.size(); ++i)
    {
        const auto& value = properties.getValueAt (i);
        text << "  " << juce::JSON::toString (juce::var (properties.getName (i).toString()), true) << ": ";

        if (const auto* list = value.getArray())
        {
            text << "[\n";

            for (int item = 0; item < list->size(); ++item)
                text << "    " << juce::JSON::toString (list->getReference (item), true)
                     << (item + 1 < list->size() ? ",\n" : "\n");

            text << "  ]";
        }
        else
        {
            text << juce::JSON::toString (value, true);
        }

        text << (i + 1 < properties.size() ? ",\n" : "\n");
    }

    text << "}\n";
    return writeTextSafely (file, text);
}

void KeymapDocument::setName (const juce::String& newName)
{
    if (auto* object = root.getDynamicObject())
        object->setProperty ("name", newName);
}

juce::Array<juce::var>* KeymapDocument::getBindings() const
{
    return root["bindings"].getArray();
}

int KeymapDocument::getNumBindings() const
{
    const auto* bindings = getBindings();
    return bindings != nullptr ? bindings->size() : 0;
}

juce::var KeymapDocument::getBinding (int index) const
{
    const auto* bindings = getBindings();
    return bindings != nullptr && juce::isPositiveAndBelow (index, bindings->size()) ? bindings->getReference (index)
                                                                                    : juce::var();
}

int KeymapDocument::addBinding (const juce::var& binding)
{
    auto* object = root.getDynamicObject();

    if (object == nullptr)
        return -1;

    if (getBindings() == nullptr)
        object->setProperty ("bindings", juce::Array<juce::var>());

    auto* bindings = getBindings();
    bindings->add (binding);
    return bindings->size() - 1;
}

void KeymapDocument::removeBinding (int index)
{
    if (auto* bindings = getBindings(); bindings != nullptr && juce::isPositiveAndBelow (index, bindings->size()))
        bindings->remove (index);
}

void KeymapDocument::setBindingProperty (int index, const juce::Identifier& name, const juce::var& value)
{
    if (auto* binding = getBinding (index).getDynamicObject())
        setOrRemove (*binding, name, value);
}
