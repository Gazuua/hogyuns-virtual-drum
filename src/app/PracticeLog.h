#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "core/PracticeStats.h"

/** 연주 로그에서 읽어 낸 "연주" 하나: 곡 하나를 틀어 두고 그에 맞춰 친 것.

    곡을 새로 열거나, 같은 곡을 앞으로 되감아 다시 틀면 새 연주로 센다.
    구간 반복으로 되돌아가는 것은 같은 연주다.
*/
struct PracticeTake
{
    juce::File logFile;
    juce::File track;                      // 틀어 둔 파일. 리듬 게임에서 드럼을 빼고 쳤으면 그 반주 파일이다
    juce::File song;                       // 원래 곡. 채보는 이것으로 찾는다
    juce::Time time;                       // 치기 시작한 때
    bool fromGame = false;                 // 리듬 게임에서 친 것인지
    int level = -1;                        // 리듬 게임의 단계 (0 ~ 3). 아니면 -1

    double firstSeconds = 0.0;             // 곡의 어디부터
    double lastSeconds = 0.0;              // 어디까지 쳤는지
    float slowestSpeed = 1.0f;             // 가장 느리게 튼 속도

    std::vector<hvd::PracticeHit> hits;    // 박자를 잴 타격. 프로그램이 대신 친 것과 누르고 있어서 이어진 타격은 뺐다
    int numPlayed = 0;                     // 사람이 낸 타격 모두 (누르고 있어서 이어진 타격 포함)
    int numByKey = 0;                      // 키보드와 페달로
    int numByMouse = 0;                    // 연주 화면의 패드를 마우스로 눌러서
    int numByMidi = 0;
    int numRolled = 0;                     // 키를 누르고 있어서 이어진 타격 (롤, 스웰). 박자를 잴 때는 뺀다

    juce::String getTitle() const          { return song.getFileNameWithoutExtension(); }
};

/** 로그 파일 하나에서 연주들을 읽는다. 시간 순이다. 타격이 minimumHits보다 적은 연주는 버린다. */
std::vector<PracticeTake> readPracticeTakes (const juce::File& logFile, int minimumHits = 20);
