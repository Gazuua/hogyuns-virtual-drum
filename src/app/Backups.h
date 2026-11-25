#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

/** 킷과 키 배열 파일의 이전 내용을 남겨 둔다. 잘못 고쳤거나 잘못 저장됐을 때 되돌리기 위한 것이다.

    파일마다 폴더 하나에 시각을 이름으로 한 사본을 쌓는다:
        <백업 폴더>/<데이터 폴더 기준 경로의 구분자를 __로 바꾼 것>/<날짜-시각><확장자>
        예: backups/kits__default__kit.json/20261125-221530.json
    (tools/의 파이썬 도구들도 kit.json을 다시 쓰기 전에 같은 자리에 같은 이름으로 남긴다.)

    남기는 규칙:
    - 가장 최근 백업과 내용이 같으면 남기지 않는다.
    - 가장 최근 백업을 남긴 지 20초가 안 됐으면 남기지 않는다. 슬라이더를 잇달아 움직일 때마다 저장되는데,
      그때는 움직이기 전의 상태 하나만 있으면 된다.
    오래된 것은 솎아 낸다: 최근 한 시간 것은 모두, 이틀까지는 한 시간에 하나, 그 뒤로는 하루에 하나를 남긴다.

    화면 스레드에서만 쓴다.
*/
class FileBackups
{
public:
    struct Entry
    {
        juce::File file;
        juce::Time time;
    };

    static constexpr int minSecondsBetween = 20;

    /** backupsRoot가 비어 있으면 아무것도 남기지 않는다. */
    void setFolders (const juce::File& backupsRoot, const juce::File& dataRootFolder);

    bool isEnabled() const                           { return root != juce::File(); }
    juce::File getRoot() const                       { return root; }

    /** original의 백업을 두는 폴더. */
    juce::File getFolderFor (const juce::File& original) const;

    /** original에 들어 있던 text를 남긴다. 남겼으면 true.
        evenIfRecent를 주면 방금 남긴 백업이 있어도 남긴다 (되돌리기 직전처럼 꼭 남겨야 할 때). */
    bool keep (const juce::File& original, const juce::String& text, bool evenIfRecent = false,
               juce::Time now = juce::Time::getCurrentTime());

    /** original의 백업들. 새것부터. */
    std::vector<Entry> list (const juce::File& original) const;

private:
    void prune (const juce::File& folder, juce::Time now) const;

    juce::File root, dataRoot;
};

/** 백업을 골라 되돌리는 메뉴를 button 아래에 띄운다. 하나를 고르면 onChoose가 그 백업 파일로 불린다. */
void showBackupMenu (juce::Component& button, const FileBackups& backups, const juce::File& original,
                     std::function<void (juce::File)> onChoose);
