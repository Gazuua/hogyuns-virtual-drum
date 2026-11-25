#pragma once

#include <juce_core/juce_core.h>

/** SFZ 가져오기의 결과. */
struct SfzImportResult
{
    juce::String error;           // 실패한 이유. 성공하면 비어 있다
    juce::String kitFolderName;   // 새로 만든 킷 폴더 이름
    int numPads = 0;
    int numSamples = 0;
    int numMissing = 0;           // SFZ에는 적혀 있지만 찾지 못한 샘플 파일 수
};

/** SFZ 악기 파일(드럼 킷)을 읽어 kitsFolder 아래에 새 킷(kit.json)을 만든다.

    건반(노트 번호) 하나가 패드 하나가 되고, 세기 구간은 레이어, 번갈아 나는 샘플은 라운드 로빈이 된다.
    General MIDI 드럼 번호에 해당하는 패드에는 기본 킷과 같은 id(kick, snare, hihat_closed, ...)를 붙여서
    기본 키 배열로 바로 칠 수 있게 한다.

    샘플 파일은 복사하지 않고 원래 자리를 가리킨다. SFZ 폴더를 옮기거나 지우면 소리가 나지 않는다.
*/
SfzImportResult importSfz (const juce::File& sfzFile, const juce::File& kitsFolder);
