#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <functional>
#include <memory>
#include <vector>

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

//==============================================================================
/** readMusic이 파일을 연 뒤 알려 주는 것. */
struct MusicInfo
{
    double sampleRate = 0.0;
    juce::int64 numFrames = 0;      // 읽을 길이 (maxSeconds로 자른 것). MP3는 어림값이라 곡이 더 일찍 끝날 수 있다
};

/** readMusic이 넘기는 덩어리 하나. 채널은 늘 둘이다 (모노 파일은 양쪽이 같다). */
struct MusicChunk
{
    const juce::AudioBuffer<float>& buffer;
    juce::int64 position;           // 이 덩어리가 곡에서 시작하는 프레임
    int numFrames;

    /** 곡이 어림한 길이보다 일찍 끝났다. 앞부분만 소리이고 나머지는 무음이다. 마지막 덩어리다. */
    bool cutShort;
};

/** 음악 파일을 처음부터 65536프레임씩 읽어 onChunk에 넘긴다. maxSeconds보다 긴 곡은 앞부분만 읽는다.
    onOpen은 첫 덩어리 전에 한 번 부른다.

    MP3 가운데에는 길이를 어림값으로만 알려 주는 파일이 있어서, 끝을 지나 읽으면 실패가 돌아온다.
    1초 넘게 읽은 뒤의 실패는 곡 끝으로 보고, 그 덩어리를 cutShort로 넘긴 뒤 멈춘다. 그 전의 실패는 오류다.

    onOpen이나 onChunk가 오류 문구를 돌려주면 거기서 멈추고 그 문구를 돌려준다. 끝까지 읽으면 빈 문자열.
*/
juce::String readMusic (const juce::File& file, double maxSeconds,
                        const std::function<juce::String (const MusicInfo&)>& onOpen,
                        const std::function<juce::String (const MusicChunk&)>& onChunk);

/** 소리 파일을 통째로 읽어 왼쪽과 오른쪽 채널로 나눠 담는다. 모노 파일이면 양쪽에 같은 소리가 들어간다.
    샘플레이트가 64 kHz를 넘으면 절반으로 줄인다. 실패하면 이유를 돌려주고, 성공하면 빈 문자열을 돌려준다.
    maxSeconds보다 긴 곡은 앞부분만 읽는다. 곡이 일찍 끝나면 읽은 데까지만 담는다. */
juce::String decodeStereo (const juce::File& file, std::vector<float>& left, std::vector<float>& right, double& sampleRate,
                           double maxSeconds = 1200.0);

/** 소리 파일을 16비트 스테레오 WAV로 풀어 쓴다 (앞 20분까지). 곡이 일찍 끝나면 그 덩어리까지 써서 끝에 무음이 조금 붙는다. */
juce::String writeAsWav (const juce::File& source, const juce::File& target);

/** WAV를 OGG(약 192 kbps)로 바꿔 쓴다. */
bool writeAsOgg (const juce::File& source, const juce::File& target);
