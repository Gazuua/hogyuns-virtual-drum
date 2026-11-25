#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

/** MP3 파일에서 음악 데이터가 실제로 시작하는 자리(바이트)를 찾는다.

    어떤 태그 편집 프로그램은 ID3 태그와 첫 MP3 프레임 사이에 자기만의 덩어리를 끼워 넣는다. 그 안의 글자
    (예: UTF-16 표시 FF FE)가 MP3 프레임의 머리처럼 보이면 JUCE의 MP3 해독기가 거기서부터 읽다가 실패한다.
    그래서 프레임 머리가 프레임 길이만큼 떨어져 세 번 이어지는 첫 자리를 찾아 돌려준다.

    JUCE가 스스로 찾는 자리(첫 ID3 태그 바로 뒤)와 같거나, 찾지 못했으면 0을 돌려준다. 그때는 손댈 것이 없다.
    stream의 읽는 위치는 바뀐다.
*/
juce::int64 findMp3AudioStart (juce::InputStream& stream);

/** 음악 파일을 읽을 리더를 만든다. MP3는 태그 뒤에 낀 군더더기를 건너뛰고 읽는다. 읽을 수 없으면 nullptr. */
std::unique_ptr<juce::AudioFormatReader> createMusicReader (juce::AudioFormatManager& formats, const juce::File& file);
