#include "MusicFile.h"

#include "Text.h"

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

//==============================================================================
juce::String readMusic (const juce::File& file, double maxSeconds,
                        const std::function<juce::String (const MusicInfo&)>& onOpen,
                        const std::function<juce::String (const MusicChunk&)>& onChunk)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto reader = createMusicReader (formats, file);

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples < 4)
        return ko ("읽을 수 없는 음악 파일입니다: ") + file.getFileName();

    const MusicInfo info { reader->sampleRate,
                           std::min (reader->lengthInSamples, static_cast<juce::int64> (maxSeconds * reader->sampleRate)) };

    if (auto error = onOpen (info); error.isNotEmpty())
        return error;

    constexpr int chunk = 65536;
    juce::AudioBuffer<float> buffer (2, chunk);

    for (juce::int64 position = 0; position < info.numFrames; position += chunk)
    {
        const auto count = static_cast<int> (std::min<juce::int64> (chunk, info.numFrames - position));

        // 실패해도 그때까지 읽은 부분은 멀쩡하고, 나머지는 무음으로 채워져 있다.
        const auto readOk = reader->read (&buffer, 0, count, position, true, true);

        if (! readOk && position < static_cast<juce::int64> (reader->sampleRate))
            return ko ("음악 파일을 읽다가 실패했습니다: ") + file.getFileName();

        if (auto error = onChunk ({ buffer, position, count, ! readOk }); error.isNotEmpty())
            return error;

        if (! readOk)
            break;
    }

    return {};
}

juce::String decodeStereo (const juce::File& file, std::vector<float>& left, std::vector<float>& right, double& sampleRate, double maxSeconds)
{
    auto halve = false;

    const auto onOpen = [&] (const MusicInfo& info)
    {
        halve = info.sampleRate > 64000.0;
        sampleRate = halve ? info.sampleRate * 0.5 : info.sampleRate;
        left.clear();
        right.clear();
        left.reserve (static_cast<std::size_t> (halve ? info.numFrames / 2 : info.numFrames));
        right.reserve (left.capacity());
        return juce::String();
    };

    const auto onChunk = [&] (const MusicChunk& chunk)
    {
        if (chunk.cutShort)
            return juce::String();    // 읽은 데까지만 쓴다

        const auto* sourceLeft = chunk.buffer.getReadPointer (0);
        const auto* sourceRight = chunk.buffer.getReadPointer (1);

        if (halve)
        {
            for (int i = 0; i + 1 < chunk.numFrames; i += 2)
            {
                left.push_back (0.5f * (sourceLeft[i] + sourceLeft[i + 1]));
                right.push_back (0.5f * (sourceRight[i] + sourceRight[i + 1]));
            }
        }
        else
        {
            left.insert (left.end(), sourceLeft, sourceLeft + chunk.numFrames);
            right.insert (right.end(), sourceRight, sourceRight + chunk.numFrames);
        }

        return juce::String();
    };

    return readMusic (file, maxSeconds, onOpen, onChunk);
}

juce::String writeAsWav (const juce::File& source, const juce::File& target)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer;

    const auto onOpen = [&] (const MusicInfo& info)
    {
        target.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = target.createOutputStream();

        if (stream != nullptr)
            writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (info.sampleRate)
                                                                                  .withNumChannels (2)
                                                                                  .withBitsPerSample (16));

        return writer == nullptr ? ko ("임시 파일을 만들 수 없습니다") : juce::String();
    };

    // 곡이 일찍 끝난 덩어리도 쓴다 (끝에 무음이 조금 붙는다). 채널이 하나뿐인 곡은 양쪽에 같은 소리가 들어 있다.
    const auto onChunk = [&] (const MusicChunk& chunk)
    {
        return writer->writeFromAudioSampleBuffer (chunk.buffer, 0, chunk.numFrames) ? juce::String() : ko ("임시 파일을 쓰다가 실패했습니다");
    };

    return readMusic (source, 1200.0, onOpen, onChunk);
}

bool writeAsOgg (const juce::File& source, const juce::File& target)
{
    auto input = source.createInputStream();

    if (input == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (input.release(), true));

    if (reader == nullptr)
        return false;

    const auto temp = target.getSiblingFile (target.getFileName() + ".part");
    temp.deleteFile();

    {
        std::unique_ptr<juce::OutputStream> stream = temp.createOutputStream();

        if (stream == nullptr)
            return false;

        juce::OggVorbisAudioFormat ogg;
        const auto quality = juce::jmin (6, ogg.getQualityOptions().size() - 1);
        const auto writer = ogg.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (reader->sampleRate)
                                                                                         .withNumChannels (static_cast<int> (reader->numChannels))
                                                                                         .withBitsPerSample (16)
                                                                                         .withQualityOptionIndex (quality));

        if (writer == nullptr || ! writer->writeFromAudioReader (*reader, 0, reader->lengthInSamples))
            return false;
    }

    return temp.moveFileTo (target);
}
