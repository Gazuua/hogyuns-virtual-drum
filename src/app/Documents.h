#pragma once

#include <juce_core/juce_core.h>

#include "core/Effects.h"

/** JSON 파일 하나를 통째로 들고 고치는 문서. 적혀 있지만 프로그램이 모르는 항목도 그대로 보존된다. */
class JsonDocument
{
public:
    /** 파일을 읽는다. 읽지 못하면(없거나, JSON 객체가 아니면) false이고 문서는 그대로다. */
    bool load (const juce::File& source);

    bool isLoaded() const noexcept    { return root.isObject(); }
    juce::File getFile() const        { return file; }

    /** 파일 안의 "name". 화면에 보이는 이름이고 파일 이름과는 따로다. */
    juce::String getName() const      { return root["name"].toString(); }
    void setName (const juce::String& newName);

protected:
    juce::File file;
    juce::var root;
};

/** kit.json을 고치고 저장하는 편집용 문서.

    엔진이 쓰는 킷(hvd::Kit)은 디코딩한 샘플까지 담은 실행용 사본이고, 이 문서는 파일 내용 그대로다.
    편집 화면은 이 문서를 고쳐 저장하고, 실행용 킷은 저장된 파일을 다시 읽어 만든다.
*/
class KitDocument : public JsonDocument
{
public:
    bool save() const;

    juce::File getFolder() const      { return file.getParentDirectory(); }

    /** id가 같은 패드를 돌려준다. 없으면 빈 값. 돌려준 값을 고치면 문서가 바뀐다. */
    juce::var findPad (const juce::String& padId) const;

    double getPadNumber (const juce::String& padId, const juce::Identifier& field, double fallback) const;
    void setPadNumber (const juce::String& padId, const juce::Identifier& field, double value);
    void setPadName (const juce::String& padId, const juce::String& newName);

    /** 패드의 항목 하나를 통째로 읽고 쓴다. (예: 그림 화면의 위치를 담는 "view") */
    juce::var getPadProperty (const juce::String& padId, const juce::Identifier& name) const;
    void setPadProperty (const juce::String& padId, const juce::Identifier& name, const juce::var& value);

    /** 킷 전체에 딸린 항목. (예: "fx", "velocityCurve") */
    juce::var getProperty (const juce::Identifier& name) const    { return root[name]; }
    void setProperty (const juce::Identifier& name, const juce::var& value);

    /** 드럼 전체에 거는 이펙트("fx"). 읽을 때 한도 밖의 값은 줄인다 (hvd::fxFields). 쓰면 "fx"를 통째로 바꾼다. */
    hvd::FxSettings getFx() const;
    void setFx (const hvd::FxSettings& fx);

    juce::StringArray getPadIds() const;

    /** 샘플이 없는 새 패드를 맨 뒤에 넣고 그 id를 돌려준다. */
    juce::String addPad (const juce::String& name);
    void removePad (const juce::String& padId);

    int countLayers (const juce::String& padId) const;
    int countSamples (const juce::String& padId) const;

    /** 패드의 샘플 구성을 사람이 읽을 수 있게 레이어마다 한 줄씩 적는다. */
    juce::StringArray describeSamples (const juce::String& padId) const;

    /** 패드의 샘플을 files로 바꾸거나(append가 false) 덧붙인다(true).
        킷 폴더 밖의 파일은 킷 폴더의 samples/<패드>/ 로 복사한다.
        문제가 있으면 이유를, 없으면 빈 문자열을 돌려준다. */
    juce::String setPadSamples (const juce::String& padId, const juce::Array<juce::File>& files, bool append);

    //==============================================================================
    // 모아 둔 소리. 패드의 "sounds"에 여러 소리를 적어 두고 그 가운데 하나를 골라 쓴다.
    // 고르면 그 소리의 샘플 구성이 패드의 "layers"가 되고, 실행용 킷은 지금처럼 "layers"만 읽는다.
    // 소리들은 tools/build_sounds.py 가 내려받은 라이브러리에서 만들어 넣는다.
    struct PadSound
    {
        juce::String id;
        juce::String name;      // 화면에 보여 줄 이름
        juce::String family;    // 같은 악기에서 나온 소리끼리 같은 값 (예: 하이햇 한 벌의 닫힘, 열림, 페달)
    };

    juce::Array<PadSound> getPadSounds (const juce::String& padId) const;

    /** 지금 쓰는 소리의 번호. 모아 둔 소리가 아니면(샘플을 직접 골랐으면) -1. */
    int getPadSoundIndex (const juce::String& padId) const;

    /** 모아 둔 소리 하나를 고른다. withFamily가 true이면 같은 악기에서 나온 소리를 가진 다른 패드도
        그 소리로 함께 바꾼다. 바뀐 패드 수를 돌려준다. */
    int selectPadSound (const juce::String& padId, int index, bool withFamily);
};

/** 키 배열 JSON을 고치고 저장하는 편집용 문서. */
class KeymapDocument : public JsonDocument
{
public:
    bool save() const;

    int getNumBindings() const;
    juce::var getBinding (int index) const;

    /** 줄 하나를 맨 뒤에 넣고 그 번호를 돌려준다. */
    int addBinding (const juce::var& binding);
    void removeBinding (int index);

    /** 줄의 항목 하나를 바꾼다. value가 비어 있으면 그 항목을 지운다. */
    void setBindingProperty (int index, const juce::Identifier& name, const juce::var& value);

    /** 줄이 치는 패드들: "pad"와 "pads"를 합친 것 (겹치는 것은 한 번만). */
    juce::StringArray getBindingPads (int index) const;

    /** 패드가 하나면 "pad", 여럿이면 "pads"로 적는다. 비어 있으면 둘 다 지운다. */
    void setBindingPads (int index, const juce::StringArray& pads);

    /** 동작(hit, choke, accent, ghost, pedal, tapTempo, metronome)을 바꾼다. 새 동작에서 쓰지 않는 항목은 지우고,
        처음 고른 동작에는 쓸 만한 값을 채운다 (멈춤은 하이햇이 아닌 심벌, 페달은 하이햇 열고 닫기). kitPadIds는 킷의 패드 id들. */
    void setBindingAction (int index, const juce::String& action, const juce::StringArray& kitPadIds);

    /** 주법(single, downUp, double, flam, drag, repeat, buzz, pattern)을 바꾼다. 다른 주법의 값은 지운다. */
    void setBindingStroke (int index, const juce::String& stroke);

private:
    juce::Array<juce::var>* getBindings() const;
};
