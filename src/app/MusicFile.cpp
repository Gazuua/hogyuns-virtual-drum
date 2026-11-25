#include "MusicFile.h"

#include <vector>

namespace
{

/** MPEG 오디오 레이어 3 프레임의 머리. */
struct Mp3Header
{
    int version = 0;       // 1, 2, 또는 25(MPEG 2.5)
    int sampleRate = 0;
    int length = 0;        // 머리를 포함한 프레임의 길이(바이트)
};

/** 네 바이트가 레이어 3 프레임의 머리이면 풀어서 돌려준다. */
bool parseMp3Header (const juce::uint8* bytes, Mp3Header& header) noexcept
{
    static constexpr int bitratesV1[] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
    static constexpr int bitratesV2[] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
    static constexpr int ratesV1[] = { 44100, 48000, 32000, 0 };

    if (bytes[0] != 0xff || (bytes[1] & 0xe0) != 0xe0)
        return false;

    const auto versionBits = (bytes[1] >> 3) & 3;
    const auto layerBits = (bytes[1] >> 1) & 3;
    const auto bitrateIndex = (bytes[2] >> 4) & 15;
    const auto rateIndex = (bytes[2] >> 2) & 3;
    const auto padding = (bytes[2] >> 1) & 1;

    // 레이어 3만 본다 (layerBits 1). 버전 1은 쓰지 않는 값이다.
    if (versionBits == 1 || layerBits != 1 || bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3)
        return false;

    header.version = versionBits == 3 ? 1 : (versionBits == 2 ? 2 : 25);
    header.sampleRate = ratesV1[rateIndex] / (header.version == 1 ? 1 : (header.version == 2 ? 2 : 4));

    const auto bitrate = (header.version == 1 ? bitratesV1 : bitratesV2)[bitrateIndex] * 1000;
    header.length = (header.version == 1 ? 144 : 72) * bitrate / header.sampleRate + padding;
    return header.length > 4;
}

/** 파일 맨 앞에 이어 붙은 ID3v2 태그들을 건너뛴 자리. firstTagEnd에는 첫 태그가 끝나는 자리를 준다. */
juce::int64 skipId3Tags (juce::InputStream& stream, juce::int64& firstTagEnd)
{
    juce::int64 position = 0;
    firstTagEnd = 0;

    for (int tag = 0; tag < 8; ++tag)
    {
        juce::uint8 head[10] {};

        if (! stream.setPosition (position) || stream.read (head, 10) != 10)
            break;

        if (head[0] != 'I' || head[1] != 'D' || head[2] != '3' || ((head[6] | head[7] | head[8] | head[9]) & 0x80) != 0)
            break;

        const auto size = (static_cast<juce::int64> (head[6]) << 21) | (static_cast<juce::int64> (head[7]) << 14)
                            | (static_cast<juce::int64> (head[8]) << 7) | static_cast<juce::int64> (head[9]);

        position += 10 + size + ((head[5] & 0x10) != 0 ? 10 : 0);    // 꼬리가 붙어 있으면 10바이트 더

        if (tag == 0)
            firstTagEnd = position;
    }

    return position;
}

} // namespace

juce::int64 findMp3AudioStart (juce::InputStream& stream)
{
    juce::int64 firstTagEnd = 0;
    const auto afterTags = skipId3Tags (stream, firstTagEnd);

    // 태그 뒤의 앞부분만 본다. 끼워 넣은 덩어리는 보통 몇 KB다.
    constexpr int windowSize = 512 * 1024;
    std::vector<juce::uint8> window (static_cast<std::size_t> (windowSize));

    if (! stream.setPosition (afterTags))
        return 0;

    const auto available = static_cast<int> (stream.read (window.data(), windowSize));

    const auto headerAt = [&window, available] (int offset, Mp3Header& header)
    {
        return offset >= 0 && offset + 4 <= available && parseMp3Header (window.data() + offset, header);
    };

    for (int offset = 0; offset + 4 <= available; ++offset)
    {
        Mp3Header first, second, third;

        if (! headerAt (offset, first) || ! headerAt (offset + first.length, second))
            continue;

        if (second.version != first.version || second.sampleRate != first.sampleRate)
            continue;

        if (! headerAt (offset + first.length + second.length, third)
            || third.version != first.version || third.sampleRate != first.sampleRate)
            continue;

        const auto start = afterTags + offset;
        return start > firstTagEnd ? start : 0;
    }

    return 0;
}

std::unique_ptr<juce::AudioFormatReader> createMusicReader (juce::AudioFormatManager& formats, const juce::File& file)
{
    if (file.hasFileExtension ("mp3"))
    {
        if (auto input = file.createInputStream())
        {
            const auto start = findMp3AudioStart (*input);
            const auto length = input->getTotalLength() - start;
            auto* format = formats.findFormatForFileExtension ("mp3");

            if (start > 0 && length > 0 && format != nullptr)
            {
                // 음악 데이터가 시작하는 자리부터만 보여 준다. 리더가 스트림을 가진다.
                auto* region = new juce::SubregionStream (input.release(), start, length, true);

                if (auto* reader = format->createReaderFor (region, true))
                    return std::unique_ptr<juce::AudioFormatReader> (reader);
            }
        }
    }

    return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file));
}
