#include "Documents.h"

#include "Json.h"
#include "core/PadKinds.h"

namespace
{

using json::loadJsonObject;
using json::writeTextSafely;

void setOrRemove (juce::DynamicObject& object, const juce::Identifier& name, const juce::var& value)
{
    if (value.isVoid())
        object.removeProperty (name);
    else
        object.setProperty (name, value);
}

// 키 배열 줄의 항목 무리 (src/core/Mapping.h의 BindingSpec). 동작이나 주법을 바꾸면 새 것에서 쓰지 않는 무리를 지운다.
// 지우는 목록이 달라지면 사용자 키 배열의 값이 사라지므로 고칠 때 조심한다 (--selftest의 checkDocuments가 본다).
constexpr const char* hitFields[] = { "velocity", "stroke", "rate", "intervalMs", "gap", "gapMs", "secondVelocity", "graceVelocity",
                                      "softVelocity", "ramp", "swellMs", "swellStart", "jitter", "velocityJitter", "accentEvery",
                                      "followDivisor", "steps", "division" };
constexpr const char* pedalFields[] = { "swap", "releasedSwap", "releasePad", "releaseVelocity", "splash", "splashMs" };
constexpr const char* repeatFields[] = { "rate", "intervalMs", "accentEvery", "softVelocity", "ramp", "followDivisor" };
constexpr const char* multiFields[] = { "gap", "gapMs", "secondVelocity", "graceVelocity" };    // 더블, 플램, 드래그
constexpr const char* patternFields[] = { "steps", "division" };

template <std::size_t size>
void removeAll (juce::DynamicObject& object, const char* const (&names)[size])
{
    for (const auto* name : names)
        object.removeProperty (name);
}

void writePads (juce::DynamicObject& binding, const juce::StringArray& pads)
{
    binding.removeProperty ("pad");
    binding.removeProperty ("pads");

    if (pads.size() == 1)
    {
        binding.setProperty ("pad", pads[0]);
    }
    else if (pads.size() > 1)
    {
        juce::Array<juce::var> list;

        for (const auto& pad : pads)
            list.add (pad);

        binding.setProperty ("pads", list);
    }
}

} // namespace

//==============================================================================
bool JsonDocument::load (const juce::File& source)
{
    juce::var parsed;

    if (! loadJsonObject (source, parsed))
        return false;

    file = source;
    root = parsed;
    return true;
}

void JsonDocument::setName (const juce::String& newName)
{
    if (auto* object = root.getDynamicObject())
        object->setProperty ("name", newName);
}

//==============================================================================
bool KitDocument::save() const
{
    return isLoaded() && writeTextSafely (file, juce::JSON::toString (root) + "\n");
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
    return json::numberOr (findPad (padId)[field], fallback);
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

hvd::FxSettings KitDocument::getFx() const
{
    const auto stored = root["fx"];
    hvd::FxSettings fx;

    for (const auto& field : hvd::fxFields)
        fx.*field.member = static_cast<float> (juce::jlimit (static_cast<double> (field.minimum), static_cast<double> (field.maximum),
                                                             json::numberOr (stored[field.name], field.fallback)));

    return fx;
}

void KitDocument::setFx (const hvd::FxSettings& fx)
{
    auto* object = new juce::DynamicObject();

    for (const auto& field : hvd::fxFields)
        object->setProperty (field.name, fx.*field.member);

    setProperty ("fx", juce::var (object));
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

juce::StringArray KeymapDocument::getBindingPads (int index) const
{
    const auto binding = getBinding (index);
    juce::StringArray pads;

    if (const auto pad = binding["pad"].toString(); pad.isNotEmpty())
        pads.add (pad);

    if (const auto* list = binding["pads"].getArray())
        for (const auto& pad : *list)
            if (pad.toString().isNotEmpty())
                pads.addIfNotAlreadyThere (pad.toString());

    return pads;
}

void KeymapDocument::setBindingPads (int index, const juce::StringArray& pads)
{
    if (auto* binding = getBinding (index).getDynamicObject())
        writePads (*binding, pads);
}

void KeymapDocument::setBindingAction (int index, const juce::String& action, const juce::StringArray& kitPadIds)
{
    auto* binding = getBinding (index).getDynamicObject();

    if (binding == nullptr)
        return;

    if (action == "hit")
        binding->removeProperty ("action");
    else
        binding->setProperty ("action", action);

    // 새 동작에서 쓰지 않는 항목은 지운다.
    if (action != "hit")
        removeAll (*binding, hitFields);

    if (action != "pedal")
        removeAll (*binding, pedalFields);

    if (action != "hit" && action != "choke")
        writePads (*binding, {});

    if (action == "hit")
    {
        if (getBindingPads (index).isEmpty() && ! kitPadIds.isEmpty())
            writePads (*binding, juce::StringArray (kitPadIds[0]));

        if (! binding->hasProperty ("velocity"))
            binding->setProperty ("velocity", 1.0);
    }
    else if (action == "choke" && getBindingPads (index).size() <= 1)
    {
        // 처음에는 하이햇이 아닌 심벌을 모두 고른다. 손으로 심벌을 잡아 멈추는 동작이기 때문이다.
        juce::StringArray cymbals;

        for (const auto& id : kitPadIds)
            if (const auto instrument = hvd::instrumentOfPad (id.toStdString()); hvd::isCymbal (instrument) && instrument != hvd::Instrument::hihat)
                cymbals.add (id);

        if (! cymbals.isEmpty())
            writePads (*binding, cymbals);
    }
    else if (action == "pedal" && ! binding->hasProperty ("swap"))
    {
        // 처음에는 하이햇 페달로 둔다: 누르는 동안 닫힌 하이햇을 열린 하이햇으로 바꾼다.
        auto* swap = new juce::DynamicObject();

        if (kitPadIds.contains ("hihat_closed") && kitPadIds.contains ("hihat_open"))
            swap->setProperty ("hihat_closed", "hihat_open");
        else if (kitPadIds.size() >= 2)
            swap->setProperty (kitPadIds[0], kitPadIds[1]);

        binding->setProperty ("swap", juce::var (swap));

        if (kitPadIds.contains ("hihat_pedal"))
            binding->setProperty ("releasePad", "hihat_pedal");
    }
}

void KeymapDocument::setBindingStroke (int index, const juce::String& stroke)
{
    auto* binding = getBinding (index).getDynamicObject();

    if (binding == nullptr)
        return;

    if (stroke == "single")
        binding->removeProperty ("stroke");
    else
        binding->setProperty ("stroke", stroke);

    // 주법마다 쓰는 항목이 다르므로 다른 주법의 값은 지운다.
    if (stroke != "repeat")
        removeAll (*binding, repeatFields);

    if (stroke != "double" && stroke != "flam" && stroke != "drag")
        removeAll (*binding, multiFields);

    if (stroke != "pattern")
        removeAll (*binding, patternFields);

    if (stroke == "repeat" && ! binding->hasProperty ("rate"))
        binding->setProperty ("rate", "1/16");

    if (stroke == "pattern" && ! binding->hasProperty ("steps"))
    {
        binding->setProperty ("steps", "X.x.");
        binding->setProperty ("division", 4);
    }
}
