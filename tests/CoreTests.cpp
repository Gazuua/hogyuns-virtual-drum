// 코어(엔진, 매핑, 키 코드, 큐) 자동 테스트.
//
// 외부 테스트 프레임워크를 쓰지 않는다. 본 프로그램에 함께 넣고 `--coretests <보고서>` 옵션으로 돌린다.

#include "CoreTests.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <string_view>
#include <vector>

#include "core/Chart.h"
#include "core/DrumTranscriber.h"
#include "core/Engine.h"
#include "core/RhythmGame.h"
#include "core/InputTiming.h"
#include "core/KeyCode.h"
#include "core/Mapping.h"
#include "core/MetronomeTiming.h"
#include "core/MouseTargets.h"
#include "core/PadKinds.h"
#include "core/Patterns.h"
#include "core/PeakLimiter.h"
#include "core/Performer.h"
#include "core/PracticeStats.h"
#include "core/Replay.h"
#include "core/SpeedTrainer.h"
#include "core/SpscQueue.h"
#include "core/StandardPads.h"
#include "core/TrackPlayer.h"

namespace
{

int numChecks = 0;
int numFailures = 0;
std::string* testLog = nullptr;

void check (bool ok, const char* expression, int line)
{
    ++numChecks;

    if (! ok)
    {
        ++numFailures;

        if (testLog != nullptr)
            *testLog += "FAIL line " + std::to_string (line) + ": " + expression + "\n";
    }
}

#define CHECK(expression) check ((expression), #expression, __LINE__)

bool isNear (float a, float b, float tolerance = 1.0e-5f)
{
    return std::fabs (a - b) <= tolerance;
}

std::shared_ptr<const hvd::Sample> makeConstantSample (float value, std::size_t frames, double sampleRate = 48000.0)
{
    auto sample = std::make_shared<hvd::Sample>();
    sample->left.assign (frames, value);
    sample->sampleRate = sampleRate;
    return sample;
}

/** 샘플 하나짜리 패드를 만든다. */
hvd::Pad makePad (const std::string& id, std::uint32_t sampleIndex)
{
    hvd::Pad pad;
    pad.id = id;
    pad.name = id;

    hvd::Layer layer;
    layer.samples.push_back (sampleIndex);
    pad.layers.push_back (layer);
    return pad;
}

struct Block
{
    std::vector<float> left, right;
};

Block render (hvd::Engine& engine, int numFrames)
{
    Block block;
    block.left.assign (static_cast<std::size_t> (numFrames), 0.0f);
    block.right.assign (static_cast<std::size_t> (numFrames), 0.0f);

    float* channels[] = { block.left.data(), block.right.data() };
    engine.render (channels, 2, numFrames);
    return block;
}

void setKit (hvd::Engine& engine, const hvd::Kit& kit)
{
    hvd::EngineCommand command;
    command.type = hvd::EngineCommand::Type::setKit;
    command.kit = &kit;
    engine.postFromControl (command);
}

void hit (hvd::Engine& engine, int pad, float velocity = 1.0f, std::uint32_t delaySamples = 0)
{
    hvd::EngineCommand command;
    command.type = hvd::EngineCommand::Type::hit;
    command.pad = static_cast<std::uint16_t> (pad);
    command.velocity = velocity;
    command.delaySamples = delaySamples;
    engine.postFromInput (command);
}

//==============================================================================
void testQueue()
{
    hvd::SpscQueue<int, 8> queue;
    int value = 0;

    CHECK (! queue.pop (value));

    for (int i = 0; i < 7; ++i)
        CHECK (queue.push (i));

    CHECK (! queue.push (99));    // 용량 8이면 7개까지 담긴다

    for (int i = 0; i < 7; ++i)
    {
        CHECK (queue.pop (value));
        CHECK (value == i);
    }

    CHECK (! queue.pop (value));

    // 끝을 넘어 처음으로 돌아가도 순서가 유지되는지
    for (int round = 0; round < 20; ++round)
    {
        CHECK (queue.push (round));
        CHECK (queue.push (round + 100));
        CHECK (queue.pop (value) && value == round);
        CHECK (queue.pop (value) && value == round + 100);
    }
}

void testKeyCodes()
{
    CHECK (hvd::keyFromName ("KeyJ") == hvd::KeyCode { 0x24 });
    CHECK (hvd::keyFromName ("keyj") == hvd::KeyCode { 0x24 });
    CHECK (hvd::keyFromName ("Space") == hvd::KeyCode { 0x39 });
    CHECK (hvd::keyFromName ("ShiftLeft") == hvd::KeyCode { 0x2a });
    CHECK (hvd::keyFromName ("ShiftRight") == hvd::KeyCode { 0x36 });
    CHECK (hvd::keyFromName ("ControlRight") == hvd::makeKeyCode (0x1d, true));
    CHECK (hvd::keyFromName ("Numpad1") == hvd::KeyCode { 0x4f });
    CHECK (hvd::keyFromName ("NumpadEnter") == hvd::makeKeyCode (0x1c, true));
    CHECK (hvd::keyFromName ("Enter") == hvd::KeyCode { 0x1c });
    CHECK (! hvd::keyFromName ("NoSuchKey").has_value());
    CHECK (! hvd::keyFromName ("").has_value());

    CHECK (hvd::keyName (0x24) == "KeyJ");
    CHECK (hvd::keyLabel (0x24) == "J");
    CHECK (hvd::keyLabel (0x4f) == "Num 1");

    // 표에 없는 키는 스캔코드 이름으로 오가야 한다
    CHECK (hvd::keyName (0x5e) == "Scan_5E");
    CHECK (hvd::keyName (hvd::makeKeyCode (0x5e, true)) == "Scan_E0_5E");
    CHECK (hvd::keyFromName ("Scan_5E") == hvd::KeyCode { 0x5e });
    CHECK (hvd::keyFromName ("Scan_E0_5E") == hvd::makeKeyCode (0x5e, true));
    CHECK (! hvd::keyFromName ("Scan_00").has_value());
    CHECK (! hvd::keyFromName ("Scan_ZZ").has_value());

    // 모든 키 번호가 이름으로 갔다가 같은 번호로 돌아오는지
    for (unsigned code = 1; code < hvd::numKeyCodes; ++code)
    {
        if ((code & 0xff) == 0)
            continue;

        const auto key = static_cast<hvd::KeyCode> (code);
        CHECK (hvd::keyFromName (hvd::keyName (key)) == key);
    }

    // 풋스위치의 스위치: 키보드가 쓰지 않는 번호에 이름이 붙어 있다
    CHECK (hvd::keyFromName ("Pedal1") == hvd::pedalKey (0));
    CHECK (hvd::keyFromName ("pedal3") == hvd::pedalKey (2));
    CHECK (hvd::keyName (hvd::pedalKey (1)) == "Pedal2");
    CHECK (hvd::keyLabel (hvd::pedalKey (hvd::maxPedalKeys - 1)) == "Pedal " + std::to_string (hvd::maxPedalKeys));
    CHECK (hvd::isPedalKey (hvd::pedalKey (0)) && hvd::isPedalKey (hvd::pedalKey (hvd::maxPedalKeys - 1)));
    CHECK (! hvd::isPedalKey (hvd::pedalKey (hvd::maxPedalKeys)) && ! hvd::isPedalKey (0x39));
    CHECK ((hvd::firstPedalKey & 0x7f) != hvd::firstPedalKey);    // 스캔코드는 0x7f까지만 쓴다

    // 마우스 옆 단추도 이름이 붙은 키다. 페달 키와 겹치지 않는다
    CHECK (hvd::keyFromName ("MouseBack") == hvd::mouseKey (0) && hvd::keyFromName ("mouseforward") == hvd::mouseKey (1));
    CHECK (hvd::keyName (hvd::mouseKey (1)) == "MouseForward");
    CHECK (hvd::isMouseKey (hvd::mouseKey (0)) && ! hvd::isPedalKey (hvd::mouseKey (0)) && ! hvd::isMouseKey (hvd::pedalKey (hvd::maxPedalKeys - 1)));
    CHECK ((hvd::mouseKey (hvd::numMouseKeys - 1) & 0x7f) != hvd::mouseKey (hvd::numMouseKeys - 1));
}

/** 보고 간격 어림: 정해진 간격의 격자 위에 놓인 시각들을 만들어 넣어 본다. */
void testReportInterval()
{
    // 사람이 밟는 것처럼 들쭉날쭉한 시각을 만들고, 장치가 gridMs마다만 알려 준다고 보고 격자에 맞춘다.
    const auto makeTimes = [] (double gridMs, double jitterMs, int count)
    {
        std::vector<double> times;
        std::uint32_t state = 12345;

        const auto random = [&state]
        {
            state = state * 1664525u + 1013904223u;
            return static_cast<double> (state >> 8) / static_cast<double> (1u << 24);    // 0 ~ 1
        };

        auto time = 1000.0;

        for (int i = 0; i < count; ++i)
        {
            time += 40.0 + 180.0 * random();    // 누르고 떼는 사이, 떼고 다시 누르는 사이
            const auto reported = gridMs > 0.0 ? std::ceil (time / gridMs) * gridMs : time;
            times.push_back (reported + jitterMs * (random() - 0.5));
        }

        return times;
    };

    for (const auto grid : { 1.0, 2.0, 4.0, 8.0, 10.0, 16.0 })
    {
        const auto times = makeTimes (grid, 0.1, 80);
        const auto found = hvd::estimateReportInterval (times);

        CHECK (found.enough);
        CHECK (found.intervalMs == grid);
        CHECK (found.shortestGapMs >= 20.0 && found.shortestGapMs <= 240.0);
    }

    // 격자가 없으면(1ms보다 훨씬 촘촘하면) 간격을 찾지 못한다
    {
        const auto found = hvd::estimateReportInterval (makeTimes (0.0, 0.0, 80));
        CHECK (found.enough);
        CHECK (found.intervalMs == 0.0);
    }

    // 조금만 모였으면 판단하지 않는다
    {
        const auto found = hvd::estimateReportInterval (makeTimes (8.0, 0.1, 10));
        CHECK (! found.enough);
        CHECK (found.numSamples == 9);
        CHECK (found.intervalMs == 0.0);
    }

    // 같은 보고에 실려 온 키(차이가 거의 0)와 한참 쉰 뒤의 키는 세지 않는다
    {
        auto times = makeTimes (8.0, 0.1, 60);
        const auto last = times.back();
        times.push_back (last + 0.03);
        times.push_back (last + 5000.0);

        const auto found = hvd::estimateReportInterval (times);
        CHECK (found.numSamples == 59);
        CHECK (found.intervalMs == 8.0);
    }

    CHECK (hvd::estimateReportInterval ({}).numSamples == 0);
}

void testMapping()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (1.0f, 100));
    kit.pads.push_back (makePad ("kick", 0));
    kit.pads.push_back (makePad ("snare", 0));
    kit.pads.push_back (makePad ("crash", 0));

    hvd::MappingSpec spec;
    spec.name = "test";
    spec.bindings.push_back ({ "KeyF", "hit", { "snare" }, 1.0f });
    spec.bindings.push_back ({ "KeyJ", "hit", { "snare" }, 0.3f });            // 한 패드에 키 여러 개
    spec.bindings.push_back ({ "Space", "hit", { "kick", "crash" }, 0.9f });   // 한 키에 패드 여러 개
    spec.bindings.push_back ({ "KeyM", "choke", { "crash" }, 1.0f });
    spec.bindings.push_back ({ "KeyQ", "hit", { "nothing" }, 1.0f });          // 없는 패드
    spec.bindings.push_back ({ "Bogus", "hit", { "kick" }, 1.0f });            // 없는 키
    spec.bindings.push_back ({ "KeyZ", "explode", { "kick" }, 1.0f });         // 없는 동작
    spec.bindings.push_back ({ "KeyX", "hit", { "kick" }, 7.0f });             // 범위를 넘는 세기

    std::vector<std::string> warnings;
    const auto mapping = hvd::resolveMapping (spec, kit, warnings);

    CHECK (mapping != nullptr);
    CHECK (warnings.size() == 3);

    const auto& onF = mapping->onPress[0x21];
    CHECK (onF.size() == 1 && onF[0].pad == 1 && onF[0].type == hvd::KeyAction::Type::hit);

    const auto& onJ = mapping->onPress[0x24];
    CHECK (onJ.size() == 1 && isNear (onJ[0].velocity, 0.3f));

    const auto& onSpace = mapping->onPress[0x39];
    CHECK (onSpace.size() == 2 && onSpace[0].pad == 0 && onSpace[1].pad == 2);

    const auto& onM = mapping->onPress[0x32];
    CHECK (onM.size() == 1 && onM[0].type == hvd::KeyAction::Type::choke && onM[0].pad == 2);

    CHECK (mapping->onPress[0x10].empty());                        // KeyQ: 없는 패드라 비어 있다
    CHECK (isNear (mapping->onPress[0x2d][0].velocity, 1.0f));      // KeyX: 1로 잘린다

    CHECK (mapping->keysForPad.size() == 3);
    CHECK (mapping->keysForPad[1].size() == 2);    // snare: F, J
    CHECK (mapping->keysForPad[2].size() == 1);    // crash: Space (choke 키는 세지 않는다)
}

void testHitAndVelocity()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (1.0f, 1000));
    kit.pads.push_back (makePad ("a", 0));
    kit.pads[0].velocityCurve = 2.0f;

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);

    CHECK (render (engine, 64).left[0] == 0.0f);    // 치기 전에는 조용하다

    hit (engine, 0, 0.5f);
    const auto block = render (engine, 64);

    CHECK (isNear (block.left[0], 0.25f));    // 0.5의 제곱
    CHECK (isNear (block.left[63], 0.25f));
    CHECK (isNear (block.right[0], 0.25f));   // 모노 샘플은 양쪽에 똑같이
    CHECK (engine.getActiveVoiceCount() == 1);
    CHECK (engine.getHitCount (0) == 1);
    CHECK (isNear (engine.getLastVelocity (0), 0.5f));

    // 샘플이 끝나면 보이스가 비워진다
    render (engine, 2000);
    CHECK (engine.getActiveVoiceCount() == 0);

    // 없는 패드를 쳐도 아무 일 없다
    hit (engine, 50);
    CHECK (render (engine, 64).left[0] == 0.0f);
}

void testOverlap()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (0.5f, 1000));
    kit.pads.push_back (makePad ("a", 0));

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);

    hit (engine, 0);
    render (engine, 64);
    hit (engine, 0);
    const auto block = render (engine, 64);

    CHECK (isNear (block.left[0], 1.0f));    // 앞 소리의 꼬리와 새 소리가 겹친다
    CHECK (engine.getActiveVoiceCount() == 2);
}

void testDelay()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (1.0f, 1000));
    kit.pads.push_back (makePad ("a", 0));

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);
    render (engine, 64);

    hit (engine, 0, 1.0f, 10);
    auto block = render (engine, 64);
    CHECK (block.left[9] == 0.0f);
    CHECK (isNear (block.left[10], 1.0f));

    // 한 블록보다 긴 지연
    render (engine, 2000);
    hit (engine, 0, 1.0f, 100);
    block = render (engine, 64);
    CHECK (block.left[63] == 0.0f);
    block = render (engine, 64);
    CHECK (block.left[35] == 0.0f);
    CHECK (isNear (block.left[36], 1.0f));
}

void testChokeGroup()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (1.0f, 10000));    // 열린 하이햇 역할
    kit.samples.push_back (makeConstantSample (0.0f, 10000));    // 닫힌 하이햇 역할 (소리 없음)
    kit.groupNames.push_back ("hihat");

    auto open = makePad ("open", 0);
    open.group = 0;
    open.cutsMask = 1;
    open.chokeFadeMs = 1.0f;    // 48kHz에서 48프레임
    kit.pads.push_back (open);

    auto closed = makePad ("closed", 1);
    closed.group = 0;
    closed.cutsMask = 1;
    kit.pads.push_back (closed);

    kit.pads.push_back (makePad ("snare", 0));    // 그룹 없음

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);

    hit (engine, 0);
    hit (engine, 2);
    auto block = render (engine, 64);
    CHECK (isNear (block.left[0], 2.0f));    // open + snare

    hit (engine, 1);
    block = render (engine, 64);
    CHECK (block.left[0] < 2.0f);             // open이 줄어들기 시작
    CHECK (block.left[10] < block.left[0]);
    CHECK (isNear (block.left[60], 1.0f));    // open은 사라지고 snare만 남는다
    CHECK (engine.getActiveVoiceCount() == 2);    // snare + closed
}

void testChokePad()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (1.0f, 10000));
    auto crash = makePad ("crash", 0);
    crash.chokeFadeMs = 1.0f;
    kit.pads.push_back (crash);
    kit.pads.push_back (makePad ("ride", 0));

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);

    hit (engine, 0);
    hit (engine, 1);
    render (engine, 64);

    hvd::EngineCommand choke;
    choke.type = hvd::EngineCommand::Type::chokePad;
    choke.pad = 0;
    engine.postFromInput (choke);

    const auto block = render (engine, 64);
    CHECK (isNear (block.left[60], 1.0f));    // crash만 멈추고 ride는 계속 울린다
    CHECK (engine.getActiveVoiceCount() == 1);

    hvd::EngineCommand allOff;
    allOff.type = hvd::EngineCommand::Type::allSoundOff;
    engine.postFromControl (allOff);
    render (engine, 4096);
    CHECK (engine.getActiveVoiceCount() == 0);
}

void testSympathetic()
{
    // 킥을 치면 스네어 울림 패드가 함께 작게 울린다. 그 패드의 타격으로는 세지 않는다.
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (1.0f, 1000));
    kit.samples.push_back (makeConstantSample (0.25f, 1000));
    auto kick = makePad ("kick", 0);
    kick.sympatheticPad = 2;
    kick.sympatheticAmount = 0.5f;
    kit.pads.push_back (kick);
    kit.pads.push_back (makePad ("snare", 0));
    kit.pads.push_back (makePad ("snare_buzz", 1));

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);
    render (engine, 16);

    hvd::HitEvent event;

    while (engine.popHitEvent (event)) {}

    hit (engine, 0);
    auto block = render (engine, 64);
    CHECK (isNear (block.left[0], 1.0f + 0.25f * 0.5f));
    CHECK (engine.getActiveVoiceCount() == 2);
    CHECK (engine.getHitCount (0) == 1 && engine.getHitCount (2) == 0);

    auto events = 0;

    while (engine.popHitEvent (event))
        ++events;

    CHECK (events == 1);

    // 스네어를 쳐도 울림 패드는 따라 울지 않는다.
    hvd::EngineCommand off;
    off.type = hvd::EngineCommand::Type::allSoundOff;
    engine.postFromControl (off);
    render (engine, 4096);
    hit (engine, 1);
    block = render (engine, 64);
    CHECK (isNear (block.left[0], 1.0f));
    CHECK (engine.getActiveVoiceCount() == 1);

    // 울림 패드를 뮤트하면 킥만, 킥을 뮤트하면 둘 다 나지 않는다.
    const auto setMute = [&engine] (int pad, bool mute)
    {
        hvd::EngineCommand command;
        command.type = hvd::EngineCommand::Type::setPadParams;
        command.pad = static_cast<std::uint16_t> (pad);
        command.padParams.mute = mute;
        engine.postFromControl (command);
    };

    engine.postFromControl (off);
    render (engine, 4096);
    setMute (2, true);
    hit (engine, 0);
    block = render (engine, 64);
    CHECK (isNear (block.left[0], 1.0f));

    engine.postFromControl (off);
    render (engine, 4096);
    setMute (2, false);
    setMute (0, true);
    hit (engine, 0);
    block = render (engine, 64);
    CHECK (isNear (block.left[0], 0.0f));
    CHECK (engine.getActiveVoiceCount() == 0);
}

void testSkipAndChokeCount()
{
    // 앞부분을 건너뛰고 낼 때는 그 자리부터 3ms 동안 키운다 (풋 스플래시).
    hvd::Kit kit;
    hvd::Sample ramp;

    for (int i = 0; i < 4000; ++i)
        ramp.left.push_back (static_cast<float> (i) / 4000.0f);

    kit.samples.push_back (std::make_shared<const hvd::Sample> (ramp));
    kit.pads.push_back (makePad ("splash", 0));

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);
    render (engine, 16);

    hvd::EngineCommand command;
    command.type = hvd::EngineCommand::Type::hit;
    command.pad = 0;
    command.skipSamples = 1000;
    engine.postFromInput (command);

    const auto block = render (engine, 300);
    CHECK (block.left[0] > 0.0f && block.left[0] < 0.25f * 0.02f);    // 작게 시작한다
    CHECK (block.left[50] < 1050.0f / 4000.0f);
    CHECK (isNear (block.left[200], 1200.0f / 4000.0f, 1.0e-4f));     // 3ms(144프레임) 뒤에는 그대로

    // 너무 많이 건너뛰면 낼 소리가 없다.
    command.skipSamples = 5000;
    engine.postFromInput (command);
    render (engine, 64);
    CHECK (engine.getActiveVoiceCount() == 1);
    CHECK (engine.getHitCount (0) == 2);

    // 멈춤 지시는 패드마다 센다.
    CHECK (engine.getChokeCount (0) == 0);
    hvd::EngineCommand choke;
    choke.type = hvd::EngineCommand::Type::chokePad;
    choke.pad = 0;
    engine.postFromInput (choke);
    render (engine, 64);
    CHECK (engine.getChokeCount (0) == 1);
    CHECK (engine.getChokeCount (-1) == 0 && engine.getChokeCount (hvd::Engine::maxPads) == 0);
}

void testRoundRobin()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (0.25f, 16));
    kit.samples.push_back (makeConstantSample (0.5f, 16));

    auto pad = makePad ("a", 0);
    pad.layers[0].samples.push_back (1);
    kit.pads.push_back (pad);

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);

    const float expected[] = { 0.25f, 0.5f, 0.25f, 0.5f };

    for (const auto value : expected)
    {
        hit (engine, 0);
        CHECK (isNear (render (engine, 32).left[0], value));
    }
}

void testLayers()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (0.1f, 16));    // 약한 타격 샘플
    kit.samples.push_back (makeConstantSample (0.8f, 16));    // 센 타격 샘플

    hvd::Pad pad;
    pad.id = "snare";

    hvd::Layer soft;
    soft.upTo = 0.5f;
    soft.nominal = 0.5f;
    soft.samples.push_back (0);

    hvd::Layer hard;
    hard.upTo = 1.0f;
    hard.nominal = 1.0f;
    hard.samples.push_back (1);

    pad.layers = { soft, hard };
    kit.pads.push_back (pad);

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);

    const auto play = [&engine] (float velocity)
    {
        hit (engine, 0, velocity);
        return render (engine, 32).left[0];
    };

    CHECK (isNear (play (0.5f), 0.1f));      // 약한 레이어를 원래 크기로
    CHECK (isNear (play (0.25f), 0.05f));    // 약한 레이어를 절반 크기로
    CHECK (isNear (play (1.0f), 0.8f));      // 센 레이어를 원래 크기로
    CHECK (isNear (play (0.75f), 0.6f));     // 센 레이어를 0.75배로
    CHECK (play (0.0f) == 0.0f);             // 세기 0은 소리를 내지 않는다
}

void testResampledPlayback()
{
    hvd::Kit kit;

    hvd::Sample ramp;
    ramp.sampleRate = 24000.0;    // 엔진 샘플레이트의 절반: 한 프레임에 0.5씩 나아간다

    for (int i = 0; i < 200; ++i)
        ramp.left.push_back (static_cast<float> (i));

    kit.samples.push_back (std::make_shared<const hvd::Sample> (ramp));
    kit.pads.push_back (makePad ("ramp", 0));

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);

    hit (engine, 0);
    auto block = render (engine, 64);
    CHECK (isNear (block.left[10], 5.0f, 1.0e-3f));
    CHECK (isNear (block.left[11], 5.5f, 1.0e-3f));

    // 재생 속도 배율 2: 한 프레임에 한 샘플씩. 엔진은 킷을 넘겨받을 때 패드 설정을 읽으므로 다시 넘긴다.
    render (engine, 2000);
    kit.pads[0].pitchRatio = 2.0f;
    setKit (engine, kit);
    hit (engine, 0);
    block = render (engine, 64);
    CHECK (isNear (block.left[10], 10.0f, 1.0e-3f));
}

void testPan()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (1.0f, 100));
    kit.pads.push_back (makePad ("right", 0));
    kit.pads.push_back (makePad ("leftish", 0));
    kit.pads[0].pan = 1.0f;
    kit.pads[1].pan = -0.5f;

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);

    hit (engine, 0);
    auto block = render (engine, 200);
    CHECK (block.left[0] == 0.0f);
    CHECK (isNear (block.right[0], 1.0f));

    hit (engine, 1);
    block = render (engine, 64);
    CHECK (isNear (block.left[0], 1.0f));
    CHECK (isNear (block.right[0], 0.5f));
}

void testVoiceStealing()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (0.001f, 100000));
    kit.pads.push_back (makePad ("a", 0));

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);

    for (int i = 0; i < 300; ++i)
    {
        hit (engine, 0);
        render (engine, 16);
    }

    render (engine, 512);    // 빼앗긴 소리가 줄어들며 끝날 때까지
    CHECK (engine.getActiveVoiceCount() == hvd::Engine::maxVoices);
    CHECK (engine.getHitCount (0) == 300);
    CHECK (engine.getStolenVoiceCount() == 300 - hvd::Engine::maxVoices);
}

void testStealOrder()
{
    // 보이스가 다 찼을 때 내주는 차례: 사라지는 중인 것 → 같은 패드의 가장 오래된 것 → 가장 작게 울리는 것.
    // 내준 소리는 3ms 동안 줄어들며 끝나서 출력이 한 프레임에 튀지 않는다.
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (0.001f, 100000));     // 보통 소리
    kit.samples.push_back (makeConstantSample (0.0f, 100000));       // 소리 없음
    kit.samples.push_back (makeConstantSample (0.0005f, 100000));    // 작은 소리
    kit.samples.push_back (makeConstantSample (0.01f, 100000));      // 큰 소리
    kit.pads.push_back (makePad ("a", 0));
    kit.pads.push_back (makePad ("silent", 1));
    kit.pads.push_back (makePad ("quiet", 2));
    auto loud = makePad ("loud", 3);
    loud.chokeFadeMs = 2000.0f;
    kit.pads.push_back (loud);

    constexpr auto others = static_cast<float> (hvd::Engine::maxVoices - 1) * 0.001f;

    // 패드 first를 먼저, second를 다음에 치고 나머지 보이스를 a로 채운다.
    const auto fill = [] (hvd::Engine& engine, int first, int second)
    {
        hit (engine, first);
        render (engine, 16);

        if (second >= 0)
        {
            hit (engine, second);
            render (engine, 16);
        }

        for (int i = second >= 0 ? 2 : 1; i < hvd::Engine::maxVoices; ++i)
        {
            hit (engine, 0);
            render (engine, 16);
        }
    };

    const auto largestStep = [] (const Block& block, float previous)
    {
        auto step = std::abs (block.left[0] - previous);

        for (std::size_t i = 1; i < block.left.size(); ++i)
            step = std::max (step, std::abs (block.left[i] - block.left[i - 1]));

        return step;
    };

    // 1. 가장 작게 울리는 것 (가장 오래된 것이 아니다). 소리 없는 패드를 쳐서 하나를 빼앗게 한다.
    {
        hvd::Engine engine;
        engine.prepare (48000.0);
        setKit (engine, kit);
        fill (engine, 0, 2);

        const auto before = render (engine, 64);
        CHECK (isNear (before.left[63], others + 0.0005f, 1.0e-4f));

        hit (engine, 1);
        const auto block = render (engine, 512);
        CHECK (engine.getStolenVoiceCount() == 1);
        CHECK (largestStep (block, before.left[63]) < 1.0e-5f);    // 0.0005를 144프레임에 나눠 줄인다
        CHECK (block.left[100] > others + 0.0001f);                // 3ms가 지나기 전에는 아직 남아 있다
        CHECK (isNear (block.left[511], others, 1.0e-4f));         // 작은 소리만 빠졌다

        // 줄어드는 중인 소리도 킷을 바꾸면 바로 멈춘다 (이전 킷의 샘플을 가리키지 않게).
        hit (engine, 1);
        render (engine, 16);
        setKit (engine, kit);
        CHECK (render (engine, 64).left[0] == 0.0f && engine.getActiveVoiceCount() == 0);
    }

    // 2. 사라지는 중인 것 (작지도 오래되지도 않았다)
    {
        hvd::Engine engine;
        engine.prepare (48000.0);
        setKit (engine, kit);
        fill (engine, 0, 3);

        hvd::EngineCommand choke;
        choke.type = hvd::EngineCommand::Type::chokePad;
        choke.pad = 3;
        engine.postFromInput (choke);
        render (engine, 64);

        hit (engine, 1);
        render (engine, 512);
        CHECK (isNear (render (engine, 64).left[0], others, 1.0e-4f));    // 큰 소리만 빠졌다
    }

    // 3. 같은 패드의 가장 오래된 것. 그 전에 친 소리는 작고 오래됐어도 남는다 (롤을 누르는 동안의 심벌).
    {
        hvd::Engine engine;
        engine.prepare (48000.0);
        setKit (engine, kit);
        fill (engine, 2, -1);

        const auto before = render (engine, 64);
        hit (engine, 0);
        render (engine, 512);
        CHECK (isNear (render (engine, 64).left[0], before.left[63], 1.0e-4f));    // a 하나가 나가고 a 하나가 들어왔다
        engine.takePadPeak (2);
        render (engine, 64);
        CHECK (isNear (engine.takePadPeak (2), 0.0005f));
    }
}

void testRollKeepsCymbals()
{
    // 버즈 롤(85ms마다 네 번 튐, 초당 47타)과 크래시 스웰(60ms 반복)을 7초 동안 함께 누르면 보이스가 모자란다.
    // 그래도 그 전에 친 라이드는 끊기지 않는다. 롤과 스웰은 자기 패드의 오래된 소리를 내준다.
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (0.001f, 48000 * 10));    // 라이드 10초
    kit.samples.push_back (makeConstantSample (0.001f, 163680));        // 스네어 3.41초
    kit.samples.push_back (makeConstantSample (0.001f, 48000 * 7));     // 크래시 7초
    kit.pads.push_back (makePad ("ride", 0));
    kit.pads.push_back (makePad ("snare", 1));
    kit.pads.push_back (makePad ("crash", 2));

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);
    hit (engine, 0);
    render (engine, 128);

    const auto start = [&engine] (std::uint32_t id, int pad, hvd::RunnerParams::Kind kind, double intervalSamples)
    {
        hvd::EngineCommand command;
        command.type = hvd::EngineCommand::Type::runnerStart;
        command.pad = static_cast<std::uint16_t> (pad);
        command.runnerId = id;
        command.runner.kind = kind;
        command.runner.intervalSamples = intervalSamples;
        engine.postFromInput (command);
    };

    start (1, 1, hvd::RunnerParams::Kind::buzz, 4080.0);
    start (2, 2, hvd::RunnerParams::Kind::repeat, 2880.0);

    auto mostVoices = 0;
    auto rideKept = true;

    for (int block = 0; block < 48000 * 7 / 128; ++block)
    {
        render (engine, 128);
        mostVoices = std::max (mostVoices, engine.getActiveVoiceCount());
        rideKept = rideKept && isNear (engine.takePadPeak (0), 0.001f);
    }

    CHECK (engine.getStolenVoiceCount() > 0);    // 보이스가 모자랐다
    CHECK (mostVoices >= hvd::Engine::maxVoices && mostVoices <= hvd::Engine::maxVoices + hvd::Engine::maxReleasingVoices);
    CHECK (rideKept);
}

void testKitSwap()
{
    hvd::Kit first, second;
    first.samples.push_back (makeConstantSample (1.0f, 10000));
    first.pads.push_back (makePad ("a", 0));
    second.samples.push_back (makeConstantSample (0.5f, 10000));
    second.pads.push_back (makePad ("a", 0));

    hvd::Engine engine;
    engine.prepare (48000.0);

    setKit (engine, first);
    hit (engine, 0);
    CHECK (isNear (render (engine, 64).left[0], 1.0f));
    CHECK (engine.popRetiredKit() == nullptr);

    setKit (engine, second);
    auto block = render (engine, 64);
    CHECK (block.left[0] == 0.0f);    // 킷을 바꾸면 울리던 소리는 멈춘다
    CHECK (engine.popRetiredKit() == &first);
    CHECK (engine.popRetiredKit() == nullptr);

    hit (engine, 0);
    CHECK (isNear (render (engine, 64).left[0], 0.5f));

    CHECK (engine.detachKit() == &second);
    hit (engine, 0);
    CHECK (render (engine, 64).left[0] == 0.0f);
}

void testQueueOverflow()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (1.0f, 100));
    kit.pads.push_back (makePad ("a", 0));

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);
    render (engine, 64);

    // 오디오가 돌지 않는 동안 지시가 넘치면 버리고 센다. 죽지 않아야 한다.
    for (int i = 0; i < 3000; ++i)
        hit (engine, 0);

    CHECK (engine.getDroppedCommandCount() > 0);
    render (engine, 64);
    CHECK (engine.getActiveVoiceCount() == hvd::Engine::maxVoices);
}

//==============================================================================
// M2: 스트로크 동작, 세기 보조, 페달, 템포

constexpr std::uint64_t msNs = 1'000'000;

constexpr hvd::KeyCode keyA = 0x1e, keyS = 0x1f, keyD = 0x20, keyTab = 0x0f, keyBackquote = 0x29, keyF1 = 0x3b;

/** 첫 프레임만 값이 있는 샘플. 타격이 시작된 프레임을 출력에서 바로 찾을 수 있다. */
std::shared_ptr<const hvd::Sample> makeImpulse (float value)
{
    auto sample = std::make_shared<hvd::Sample>();
    sample->left.assign (8, 0.0f);
    sample->left[0] = value;
    return sample;
}

/** 임펄스 패드 세 개(a: 1, b: 0.5, c: 0.25). 울린 크기로 어느 패드인지 알 수 있다. */
hvd::Kit makeImpulseKit()
{
    hvd::Kit kit;
    kit.samples.push_back (makeImpulse (1.0f));
    kit.samples.push_back (makeImpulse (0.5f));
    kit.samples.push_back (makeImpulse (0.25f));
    kit.pads.push_back (makePad ("a", 0));
    kit.pads.push_back (makePad ("b", 1));
    kit.pads.push_back (makePad ("c", 2));
    return kit;
}

struct Onset
{
    int frame;
    float value;
};

std::vector<Onset> findOnsets (const Block& block)
{
    std::vector<Onset> onsets;

    for (std::size_t i = 0; i < block.left.size(); ++i)
        if (block.left[i] > 1.0e-4f)
            onsets.push_back ({ static_cast<int> (i), block.left[i] });

    return onsets;
}

bool onsetIs (const std::vector<Onset>& onsets, std::size_t index, int frame, float value)
{
    return index < onsets.size() && onsets[index].frame == frame && isNear (onsets[index].value, value, 1.0e-3f);
}

hvd::BindingSpec bind (const char* key, const char* pad, float velocity = 1.0f, const char* stroke = "single")
{
    hvd::BindingSpec binding;
    binding.key = key;
    binding.pads = { pad };
    binding.velocity = velocity;
    binding.stroke = stroke;
    binding.jitter = 0.0f;            // 테스트에서는 흔들림을 끈다
    binding.velocityJitter = 0.0f;
    return binding;
}

hvd::BindingSpec bindAction (const char* key, const char* action)
{
    hvd::BindingSpec binding;
    binding.key = key;
    binding.action = action;
    return binding;
}

/** 엔진, 연주 로직, 임펄스 킷을 묶어 둔 것. */
struct Rig
{
    explicit Rig (const hvd::MappingSpec& spec)
    {
        engine.prepare (48000.0);
        setKit (engine, kit);
        render (engine, 16);
        performer.setMapping (hvd::resolveMapping (spec, kit, warnings));
    }

    std::vector<Onset> play (int numFrames)    { return findOnsets (render (engine, numFrames)); }

    hvd::Kit kit = makeImpulseKit();
    hvd::Engine engine;
    hvd::Performer performer { engine };
    std::vector<std::string> warnings;
};

void testParseBeats()
{
    CHECK (isNear (hvd::parseBeats ("1/4"), 1.0f));
    CHECK (isNear (hvd::parseBeats ("1/8"), 0.5f));
    CHECK (isNear (hvd::parseBeats ("1/16"), 0.25f));
    CHECK (isNear (hvd::parseBeats ("1/32"), 0.125f));
    CHECK (isNear (hvd::parseBeats ("1/8t"), 1.0f / 3.0f));
    CHECK (isNear (hvd::parseBeats ("1/8d"), 0.75f));
    CHECK (isNear (hvd::parseBeats ("3/16"), 0.75f));
    CHECK (hvd::parseBeats ("") == 0.0f);
    CHECK (hvd::parseBeats ("16") == 0.0f);
    CHECK (hvd::parseBeats ("1/") == 0.0f);
    CHECK (hvd::parseBeats ("/8") == 0.0f);
    CHECK (hvd::parseBeats ("1/8x") == 0.0f);
    CHECK (hvd::parseBeats ("a/b") == 0.0f);
}

void testStrokeResolve()
{
    const auto kit = makeImpulseKit();

    hvd::MappingSpec spec;
    spec.bindings.push_back (bind ("KeyA", "a", 1.0f, "double"));
    spec.bindings.push_back (bind ("KeyS", "a", 1.0f, "repeat"));
    spec.bindings.back().rate = "1/8t";
    spec.bindings.push_back (bind ("KeyD", "a", 1.0f, "pattern"));
    spec.bindings.back().steps = "X.x. | o...";
    spec.bindings.back().division = 2.0f;
    spec.bindings.push_back (bind ("KeyF", "a", 1.0f, "wobble"));     // 없는 타격 방식
    spec.bindings.push_back (bind ("KeyG", "a", 1.0f, "pattern"));    // 스텝이 없는 패턴
    spec.bindings.push_back (bind ("KeyH", "a", 1.0f, "repeat"));
    spec.bindings.back().rate = "fast";                               // 읽을 수 없는 간격

    std::vector<std::string> warnings;
    const auto mapping = hvd::resolveMapping (spec, kit, warnings);

    CHECK (warnings.size() == 3);

    const auto& doubleStroke = mapping->onPress[keyA][0].stroke;
    CHECK (doubleStroke.type == hvd::Stroke::Type::multi && doubleStroke.numExtra == 1);
    CHECK (isNear (doubleStroke.extra[0].delay.value, 85.0f) && isNear (doubleStroke.extra[0].velocityScale, 0.8f));

    const auto& repeatStroke = mapping->onPress[keyS][0].stroke;
    CHECK (repeatStroke.type == hvd::Stroke::Type::repeat);
    CHECK (repeatStroke.interval.kind == hvd::Interval::Kind::beats && isNear (repeatStroke.interval.value, 1.0f / 3.0f));

    const auto& patternStroke = mapping->onPress[keyD][0].stroke;
    CHECK (patternStroke.type == hvd::Stroke::Type::pattern && patternStroke.numSteps == 8);
    CHECK (patternStroke.steps[0] == 255 && patternStroke.steps[1] == 0 && patternStroke.steps[2] == 190 && patternStroke.steps[4] == 115);
    CHECK (isNear (patternStroke.stepBeats, 0.5f));

    CHECK (mapping->onPress[0x21].empty());    // KeyF: 없는 방식이라 버려진다
    CHECK (mapping->onPress[0x22].empty());    // KeyG: 스텝이 없어 버려진다

    // 읽을 수 없는 간격은 경고하고 기본 간격(16분음표)으로 둔다
    CHECK (mapping->onPress[0x23].size() == 1);
    CHECK (isNear (mapping->onPress[0x23][0].stroke.interval.value, 0.25f));
}

void testSingleAndDownUp()
{
    hvd::MappingSpec spec;
    spec.chordAccentMs = 0.0f;
    spec.bindings.push_back (bind ("KeyA", "a", 0.5f));
    spec.bindings.push_back (bind ("KeyS", "a", 1.0f, "downUp"));

    Rig rig (spec);
    CHECK (rig.warnings.empty());

    rig.performer.keyDown (keyA, 1000 * msNs);
    auto onsets = rig.play (64);
    CHECK (onsets.size() == 1 && onsetIs (onsets, 0, 0, 0.5f));

    rig.performer.keyUp (keyA, 1010 * msNs);
    CHECK (rig.play (64).empty());    // 단타는 뗄 때 아무 일도 없다

    rig.performer.keyDown (keyS, 1100 * msNs);
    onsets = rig.play (64);
    CHECK (onsets.size() == 1 && onsetIs (onsets, 0, 0, 1.0f));

    rig.performer.keyUp (keyS, 1200 * msNs);
    onsets = rig.play (64);
    CHECK (onsets.size() == 1 && onsetIs (onsets, 0, 0, 0.8f));    // 뗄 때 한 번 더
}

void testDoubleAndFlam()
{
    hvd::MappingSpec spec;
    spec.chordAccentMs = 0.0f;
    spec.bindings.push_back (bind ("KeyA", "a", 1.0f, "double"));
    spec.bindings.back().gapMs = 10.0f;    // 48kHz에서 480프레임
    spec.bindings.push_back (bind ("KeyS", "a", 1.0f, "flam"));
    spec.bindings.push_back (bind ("KeyD", "a", 1.0f, "double"));
    spec.bindings.back().gap = "1/16";     // 템포 120에서 6000프레임
    spec.bindings.back().secondVelocity = 0.5f;

    Rig rig (spec);

    rig.performer.keyDown (keyA, 1000 * msNs);
    auto onsets = rig.play (1000);
    CHECK (onsets.size() == 2 && onsetIs (onsets, 0, 0, 1.0f) && onsetIs (onsets, 1, 480, 0.8f));

    rig.performer.keyDown (keyS, 2000 * msNs);
    onsets = rig.play (2000);
    CHECK (onsets.size() == 2 && onsetIs (onsets, 0, 0, 0.35f) && onsetIs (onsets, 1, 1344, 1.0f));    // 꾸밈음 뒤 28ms에 본음

    rig.performer.keyDown (keyD, 3000 * msNs);
    onsets = rig.play (7000);
    CHECK (onsets.size() == 2 && onsetIs (onsets, 0, 0, 1.0f) && onsetIs (onsets, 1, 6000, 0.5f));
}

void testRepeat()
{
    hvd::MappingSpec spec;
    spec.chordAccentMs = 0.0f;
    spec.bindings.push_back (bind ("KeyA", "a", 1.0f, "repeat"));
    spec.bindings.back().intervalMs = 10.0f;
    spec.bindings.back().accentEvery = 2;
    spec.bindings.back().softVelocity = 0.5f;

    Rig rig (spec);

    rig.performer.keyDown (keyA, 1000 * msNs);
    auto onsets = rig.play (2000);
    CHECK (onsets.size() == 5);
    CHECK (onsetIs (onsets, 0, 0, 1.0f) && onsetIs (onsets, 1, 480, 0.5f) && onsetIs (onsets, 2, 960, 1.0f));
    CHECK (onsetIs (onsets, 3, 1440, 0.5f) && onsetIs (onsets, 4, 1920, 1.0f));
    CHECK (rig.engine.getActiveRunnerCount() == 1);

    rig.performer.keyUp (keyA, 1050 * msNs);
    CHECK (rig.play (2000).empty());    // 떼면 멈춘다
    CHECK (rig.engine.getActiveRunnerCount() == 0);

    // 악센트 주기를 적지 않으면 첫 타만 원래 세기다
    hvd::MappingSpec plain;
    plain.bindings.push_back (bind ("KeyA", "a", 1.0f, "repeat"));
    plain.bindings.back().intervalMs = 10.0f;

    Rig plainRig (plain);
    plainRig.performer.keyDown (keyA, 1000 * msNs);
    onsets = plainRig.play (1000);
    CHECK (onsets.size() == 3 && onsetIs (onsets, 0, 0, 1.0f) && onsetIs (onsets, 1, 480, 0.8f) && onsetIs (onsets, 2, 960, 0.8f));
}

void testRepeatSwell()
{
    // 반복에 "점점 커지기"를 붙인 것(심벌 스웰): 정한 시간에 걸쳐 작은 세기에서 원래 세기로 올라간 뒤 그 세기로 이어진다
    hvd::MappingSpec spec;
    spec.chordAccentMs = 0.0f;
    spec.bindings.push_back (bind ("KeyA", "a", 0.8f, "repeat"));
    spec.bindings.back().intervalMs = 10.0f;      // 480프레임
    spec.bindings.back().softVelocity = 0.8f;     // 모든 타격을 같은 세기로
    spec.bindings.back().swellMs = 40.0f;         // 1920프레임
    spec.bindings.back().swellStart = 0.25f;

    Rig rig (spec);
    CHECK (rig.warnings.empty());

    rig.performer.keyDown (keyA, 1000 * msNs);
    auto onsets = rig.play (3000);

    // 0.25배에서 시작한다. dB로 곧게 올리므로 절반 지점에서는 0.5배(0.25의 제곱근)다. 다 커진 뒤로는 1배로 이어진다.
    CHECK (onsets.size() == 7);
    CHECK (onsetIs (onsets, 0, 0, 0.2f) && onsetIs (onsets, 1, 480, 0.2828f) && onsetIs (onsets, 2, 960, 0.4f));
    CHECK (onsetIs (onsets, 3, 1440, 0.5657f) && onsetIs (onsets, 4, 1920, 0.8f));
    CHECK (onsetIs (onsets, 5, 2400, 0.8f) && onsetIs (onsets, 6, 2880, 0.8f));

    // 떼면 멈추고, 다시 누르면 다시 작은 데서 시작한다
    rig.performer.keyUp (keyA, 1100 * msNs);
    CHECK (rig.play (2000).empty());

    rig.performer.keyDown (keyA, 2000 * msNs);
    onsets = rig.play (100);
    CHECK (onsets.size() == 1 && onsetIs (onsets, 0, 0, 0.2f));

    // 시작 배율을 적지 않으면 0.4배에서 시작한다
    hvd::MappingSpec plain;
    plain.chordAccentMs = 0.0f;
    plain.bindings.push_back (bind ("KeyA", "a", 1.0f, "repeat"));
    plain.bindings.back().intervalMs = 10.0f;
    plain.bindings.back().softVelocity = 1.0f;
    plain.bindings.back().swellMs = 20.0f;        // 960프레임

    Rig plainRig (plain);
    plainRig.performer.keyDown (keyA, 1000 * msNs);
    onsets = plainRig.play (1500);
    CHECK (onsets.size() == 4);
    CHECK (onsetIs (onsets, 0, 0, 0.4f) && onsetIs (onsets, 1, 480, 0.6325f) && onsetIs (onsets, 2, 960, 1.0f) && onsetIs (onsets, 3, 1440, 1.0f));
}

void testRepeatFollowsTempo()
{
    hvd::MappingSpec spec;
    spec.bindings.push_back (bind ("KeyA", "a", 1.0f, "repeat"));
    spec.bindings.back().rate = "1/16";
    spec.bindings.back().softVelocity = 1.0f;

    Rig rig (spec);
    rig.engine.setTempo (120.0f);    // 16분음표 = 6000프레임

    rig.performer.keyDown (keyA, 1000 * msNs);
    auto onsets = rig.play (13000);
    CHECK (onsets.size() == 3 && onsets[1].frame == 6000 && onsets[2].frame == 12000);

    // 템포를 두 배로 올리면 다음 간격부터 절반이 된다
    rig.engine.setTempo (240.0f);
    onsets = rig.play (13000);
    CHECK (onsets.size() == 3 && onsets[0].frame == 5000 && onsets[1].frame == 8000 && onsets[2].frame == 11000);
}

void testRepeatFollow()
{
    hvd::MappingSpec spec;
    spec.chordAccentMs = 0.0f;
    spec.bindings.push_back (bind ("KeyA", "a", 1.0f));
    spec.bindings.push_back (bind ("KeyS", "a", 1.0f, "repeat"));
    spec.bindings.back().rate = "follow";
    spec.bindings.back().softVelocity = 1.0f;

    {
        // 직전에 친 간격(100ms = 4800프레임)을 그대로 이어 간다
        Rig rig (spec);
        rig.performer.keyDown (keyA, 1000 * msNs);
        rig.play (64);
        rig.performer.keyDown (keyS, 1100 * msNs);
        const auto onsets = rig.play (10000);
        CHECK (onsets.size() == 3 && onsets[0].frame == 0 && onsets[1].frame == 4800 && onsets[2].frame == 9600);
    }

    {
        // 직전에 친 적이 없으면 한 번만 친다
        Rig rig (spec);
        rig.performer.keyDown (keyS, 1000 * msNs);
        const auto onsets = rig.play (10000);
        CHECK (onsets.size() == 1 && rig.engine.getActiveRunnerCount() == 0);
    }

    {
        // 간격이 너무 벌어져도 한 번만 친다
        Rig rig (spec);
        rig.performer.keyDown (keyA, 1000 * msNs);
        rig.play (64);
        rig.performer.keyDown (keyS, 3000 * msNs);
        CHECK (rig.play (10000).size() == 1);
    }

    {
        // followDivisor 2: 잰 간격의 절반으로
        auto faster = spec;
        faster.bindings.back().followDivisor = 2.0f;

        Rig rig (faster);
        rig.performer.keyDown (keyA, 1000 * msNs);
        rig.play (64);
        rig.performer.keyDown (keyS, 1100 * msNs);
        const auto onsets = rig.play (5000);
        CHECK (onsets.size() == 3 && onsets[1].frame == 2400 && onsets[2].frame == 4800);
    }
}

void testBuzz()
{
    hvd::MappingSpec spec;
    spec.bindings.push_back (bind ("KeyA", "a", 1.0f, "buzz"));
    spec.bindings.back().intervalMs = 50.0f;    // 손 스트로크 간격 2400프레임

    Rig rig (spec);
    rig.performer.keyDown (keyA, 1000 * msNs);
    auto onsets = rig.play (3000);

    // 스트로크 하나가 네 번 튄다: 간격 24ms에서 0.82배씩 줄고, 세기는 0.72배씩 줄어든다.
    // 넷째 튐(2871)은 다음 스트로크(2400)보다 뒤에 와서 서로 겹친다.
    CHECK (onsets.size() == 5);
    CHECK (onsetIs (onsets, 0, 0, 1.0f) && onsetIs (onsets, 1, 1152, 0.72f) && onsetIs (onsets, 2, 2096, 0.5184f));
    CHECK (onsetIs (onsets, 3, 2400, 1.0f) && onsetIs (onsets, 4, 2871, 0.3732f));

    rig.performer.keyUp (keyA, 1100 * msNs);
    rig.play (20000);
    CHECK (rig.play (5000).empty());

    // 점점 커지는 롤: 100ms(4800프레임)에 걸쳐 0.4배에서 1배로
    auto swelling = spec;
    swelling.bindings.back().swellMs = 100.0f;

    Rig swellRig (swelling);
    swellRig.performer.keyDown (keyA, 1000 * msNs);
    onsets = swellRig.play (4900);
    CHECK (onsetIs (onsets, 0, 0, 0.4f));

    auto sawMiddle = false, sawFull = false;

    for (const auto& onset : onsets)
    {
        sawMiddle = sawMiddle || (onset.frame == 2400 && isNear (onset.value, 0.6325f, 1.0e-3f));    // dB로 곧게: 0.4의 제곱근
        sawFull = sawFull || (onset.frame == 4800 && isNear (onset.value, 1.0f, 1.0e-3f));
    }

    CHECK (sawMiddle && sawFull);
}

void testPattern()
{
    hvd::MappingSpec spec;
    spec.bindings.push_back (bind ("KeyA", "a", 1.0f, "pattern"));
    spec.bindings.back().steps = "X.x.";    // 16분음표 네 스텝, 템포 120에서 스텝 하나가 6000프레임

    Rig rig (spec);
    rig.engine.setTempo (120.0f);

    rig.performer.keyDown (keyA, 1000 * msNs);
    rig.performer.keyUp (keyA, 1050 * msNs);    // 떼도 계속 돈다
    auto onsets = rig.play (25000);
    CHECK (onsets.size() == 3);
    CHECK (onsetIs (onsets, 0, 0, 1.0f) && onsetIs (onsets, 1, 12000, 190.0f / 255.0f) && onsetIs (onsets, 2, 24000, 1.0f));
    CHECK (rig.engine.getActiveRunnerCount() == 1);

    rig.performer.keyDown (keyA, 2000 * msNs);    // 다시 누르면 꺼진다
    CHECK (rig.play (25000).empty());
    CHECK (rig.engine.getActiveRunnerCount() == 0);

    rig.performer.keyDown (keyA, 3000 * msNs);    // 또 누르면 처음부터 다시
    onsets = rig.play (100);
    CHECK (onsets.size() == 1 && onsetIs (onsets, 0, 0, 1.0f));
}

void testModifiers()
{
    hvd::MappingSpec spec;
    spec.chordAccentMs = 0.0f;
    spec.bindings.push_back (bindAction ("Tab", "accent"));
    spec.bindings.push_back (bindAction ("Backquote", "ghost"));
    spec.bindings.push_back (bind ("KeyA", "a", 0.5f));
    spec.bindings.push_back (bind ("KeyS", "a", 0.5f, "repeat"));
    spec.bindings.back().intervalMs = 10.0f;
    spec.bindings.back().softVelocity = 0.5f;    // 악센트가 아닌 타격도 같은 세기

    Rig rig (spec);

    const auto hitValue = [&rig] (std::uint64_t timeMs)
    {
        rig.performer.keyDown (keyA, timeMs * msNs);
        const auto onsets = rig.play (64);
        return onsets.size() == 1 ? onsets[0].value : -1.0f;
    };

    CHECK (isNear (hitValue (1000), 0.5f));

    rig.performer.keyDown (keyTab, 1100 * msNs);
    CHECK (isNear (hitValue (1200), 0.75f));     // 악센트: 1.5배

    rig.performer.keyDown (keyBackquote, 1300 * msNs);
    CHECK (isNear (hitValue (1400), 0.5f));      // 둘 다 누르면 상쇄

    rig.performer.keyUp (keyTab, 1500 * msNs);
    CHECK (isNear (hitValue (1600), 0.2f));      // 고스트: 0.4배

    rig.performer.keyUp (keyBackquote, 1700 * msNs);
    CHECK (isNear (hitValue (1800), 0.5f));

    // 반복 중에 보조 키를 누르면 그때부터 세기가 바뀐다
    rig.performer.keyDown (keyS, 2000 * msNs);
    auto onsets = rig.play (960);
    CHECK (onsets.size() == 2 && isNear (onsets[1].value, 0.5f));

    rig.performer.keyDown (keyTab, 2020 * msNs);
    onsets = rig.play (960);
    CHECK (onsets.size() == 2 && isNear (onsets[0].value, 0.75f) && isNear (onsets[1].value, 0.75f));

    rig.performer.keyUp (keyTab, 2040 * msNs);
    onsets = rig.play (960);
    CHECK (onsets.size() == 2 && isNear (onsets[0].value, 0.5f));
}

void testPedal()
{
    hvd::MappingSpec spec;
    spec.chordAccentMs = 0.0f;
    spec.bindings.push_back (bind ("KeyA", "a"));

    auto pedal = bindAction ("Tab", "pedal");
    pedal.swap = { { "a", "b" } };
    pedal.releasePad = "c";
    spec.bindings.push_back (pedal);

    auto pedalWithSound = bindAction ("Backquote", "pedal");
    pedalWithSound.pads = { "c" };
    spec.bindings.push_back (pedalWithSound);

    Rig rig (spec);
    CHECK (rig.warnings.empty());

    const auto press = [&rig] (hvd::KeyCode key, std::uint64_t timeMs)
    {
        rig.performer.keyDown (key, timeMs * msNs);
        const auto onsets = rig.play (64);
        return onsets.size() == 1 ? onsets[0].value : -1.0f;
    };

    CHECK (isNear (press (keyA, 1000), 1.0f));       // 평소에는 a
    CHECK (press (keyTab, 1100) < 0.0f);             // 이 페달은 누를 때 소리가 없다
    CHECK (isNear (press (keyA, 1200), 0.5f));       // 페달을 누르는 동안에는 b

    rig.performer.keyUp (keyTab, 1300 * msNs);
    auto onsets = rig.play (64);
    CHECK (onsets.size() == 1 && isNear (onsets[0].value, 0.25f));    // 뗄 때 c
    CHECK (isNear (press (keyA, 1400), 1.0f));       // 다시 a

    CHECK (isNear (press (keyBackquote, 1500), 0.25f));    // 누를 때 소리가 나는 페달
}

void testPedalReleasedSwap()
{
    // 하이햇 페달처럼 둔다: 떼고 있으면 a를 b로(반열림), 밟고 있으면 a 그대로(닫힘), 밟을 때 c(칙).
    hvd::MappingSpec spec;
    spec.chordAccentMs = 0.0f;
    spec.bindings.push_back (bind ("KeyA", "a"));

    auto foot = bindAction ("Pedal1", "pedal");
    foot.pads = { "c" };
    foot.releasedSwap = { { "a", "b" } };
    spec.bindings.push_back (foot);

    auto open = bindAction ("Tab", "pedal");
    open.swap = { { "a", "c" } };
    spec.bindings.push_back (open);

    // 키보드 키의 페달은 풋스위치와 상관없이 "떼고 있는 동안" 바꿈을 쓴다.
    auto keyboardPedal = bindAction ("KeyD", "pedal");
    keyboardPedal.releasedSwap = { { "b", "c" } };
    spec.bindings.push_back (keyboardPedal);

    Rig rig (spec);
    CHECK (rig.warnings.empty());

    const auto press = [&rig] (hvd::KeyCode key, std::uint64_t timeMs)
    {
        rig.performer.keyDown (key, timeMs * msNs);
        const auto onsets = rig.play (64);
        return onsets.size() == 1 ? onsets[0].value : -1.0f;
    };

    const auto pedal = hvd::pedalKey (0);
    const auto mapping = rig.performer.getMapping();
    CHECK (mapping != nullptr && mapping->keysForPad[2].size() == 1 && mapping->keysForPad[2][0] == pedal);    // 화면에 페달 키를 적는다

    // 풋스위치가 없으면 Pedal1의 "떼고 있는 동안" 바꿈은 쓰지 않는다. 페달 없이 칠 때는 전과 같다.
    CHECK (isNear (press (keyA, 900), 1.0f));
    CHECK (rig.performer.swappedForScreen (0) == 0);
    CHECK (rig.performer.swappedForScreen (1) == 2);
    rig.performer.setFootswitchConnected (true);

    CHECK (rig.performer.swappedForScreen (0) == 1);    // 마우스로 쳐도 떼고 있으면 b
    CHECK (isNear (press (keyA, 1000), 0.5f));          // 떼고 있으면 b
    CHECK (isNear (press (pedal, 1100), 0.25f));        // 밟을 때 c
    CHECK (isNear (press (keyA, 1200), 1.0f));          // 밟고 있는 동안 a
    CHECK (rig.performer.swappedForScreen (0) == 0);
    CHECK (rig.performer.swappedForScreen (2) == 2);    // 바꿈에 없는 패드는 그대로

    rig.performer.keyUp (pedal, 1300 * msNs);
    CHECK (rig.play (64).empty());                      // 뗄 때 칠 패드는 없다
    CHECK (isNear (press (keyA, 1400), 0.5f));
    CHECK (rig.performer.swappedForScreen (0) == 1);

    // 누르는 동안의 바꿈이 떼고 있는 동안의 바꿈보다 먼저다.
    CHECK (press (keyTab, 1500) < 0.0f);
    CHECK (isNear (press (keyA, 1600), 0.25f));
    CHECK (rig.performer.swappedForScreen (0) == 2);
}

void testFootSplash()
{
    // 밟을 때 c(칙). 150ms 안에 떼면 b(스플래시), 늦게 떼면 a(뗄 때 칠 패드).
    hvd::MappingSpec spec;
    spec.chordAccentMs = 0.0f;

    auto foot = bindAction ("Pedal1", "pedal");
    foot.pads = { "c" };
    foot.splash = "b";
    foot.releasePad = "a";
    spec.bindings.push_back (foot);

    auto quickOnly = bindAction ("Tab", "pedal");
    quickOnly.splash = "b";
    quickOnly.splashMs = 5000.0f;    // 1초로 줄인다
    spec.bindings.push_back (quickOnly);

    Rig rig (spec);
    CHECK (rig.warnings.empty());

    const auto pedal = hvd::pedalKey (0);
    const auto mapping = rig.performer.getMapping();
    CHECK (mapping != nullptr && mapping->pedals.size() == 2);
    CHECK (mapping != nullptr && mapping->pedals[0].splashPad == 1 && isNear (mapping->pedals[0].splashMs, 150.0f));
    CHECK (mapping != nullptr && isNear (mapping->pedals[1].splashMs, 1000.0f));
    CHECK (mapping != nullptr && std::find (mapping->keysForPad[1].begin(), mapping->keysForPad[1].end(), pedal) != mapping->keysForPad[1].end());

    const auto counts = [&rig]
    {
        return std::array<std::uint32_t, 3> { rig.engine.getHitCount (0), rig.engine.getHitCount (1), rig.engine.getHitCount (2) };
    };

    rig.performer.keyDown (pedal, 1000 * msNs);
    rig.play (64);
    CHECK ((counts() == std::array<std::uint32_t, 3> { 0, 0, 1 }));

    rig.performer.keyUp (pedal, 1080 * msNs);    // 80ms 만에 뗐다
    rig.play (64);
    CHECK ((counts() == std::array<std::uint32_t, 3> { 0, 1, 1 }));

    rig.performer.keyDown (pedal, 2000 * msNs);
    rig.performer.keyUp (pedal, 2400 * msNs);    // 400ms 밟고 있었다
    rig.play (64);
    CHECK ((counts() == std::array<std::uint32_t, 3> { 1, 1, 2 }));

    rig.performer.keyDown (keyTab, 3000 * msNs);
    rig.performer.keyUp (keyTab, 3900 * msNs);
    rig.performer.keyDown (keyTab, 4000 * msNs);
    rig.performer.keyUp (keyTab, 5200 * msNs);   // 뗄 때 칠 패드가 없으면 늦게 뗐을 때는 아무것도 치지 않는다
    rig.play (64);
    CHECK ((counts() == std::array<std::uint32_t, 3> { 1, 2, 2 }));
}

void testMouseHit()
{
    // 입력 스레드가 받은 마우스 클릭: 키처럼 페달을 따르거나(보통 자리), 따르지 않는다(하이햇 닫힘 고정).
    hvd::MappingSpec spec;
    spec.chordAccentMs = 0.0f;

    auto foot = bindAction ("Pedal1", "pedal");
    foot.releasedSwap = { { "a", "b" } };
    spec.bindings.push_back (foot);

    Rig rig (spec);
    rig.performer.setFootswitchConnected (true);

    const auto click = [&rig] (std::uint16_t pad, bool followPedals)
    {
        rig.performer.mouseHit (pad, 0.9f, followPedals);
        const auto onsets = rig.play (64);
        return onsets.size() == 1 ? onsets[0].value : -1.0f;
    };

    hvd::HitEvent event;

    while (rig.engine.popHitEvent (event)) {}

    CHECK (isNear (click (0, true), 0.45f));     // 페달을 떼고 있으면 b
    CHECK (rig.engine.popHitEvent (event) && event.origin.source == hvd::HitSource::mouse && event.pad == 1);
    CHECK (isNear (click (0, false), 0.9f));     // 고정 자리는 a 그대로

    rig.performer.keyDown (hvd::pedalKey (0), 1000 * msNs);
    rig.play (64);
    CHECK (isNear (click (0, true), 0.9f));      // 밟고 있으면 a
    CHECK (isNear (click (2, true), 0.225f));    // 바꿈에 없는 패드는 그대로

    const std::uint16_t chokes[] = { 0, 2 };
    rig.performer.choke (chokes);
    rig.play (64);
    CHECK (rig.engine.getChokeCount (0) == 1 && rig.engine.getChokeCount (1) == 0 && rig.engine.getChokeCount (2) == 1);
}

void testMouseTargets()
{
    hvd::MouseTargets targets;
    targets.left = 0.0f;
    targets.top = 0.0f;
    targets.right = 200.0f;
    targets.bottom = 100.0f;

    hvd::MouseTarget bell;    // 라이드 위에 얹힌 작은 자리가 먼저다
    bell.shape = hvd::MouseTarget::Shape::ellipse;
    bell.left = 90.0f;
    bell.top = 40.0f;
    bell.right = 110.0f;
    bell.bottom = 60.0f;
    bell.pad = 2;

    auto ride = bell;
    ride.left = 40.0f;
    ride.top = 10.0f;
    ride.right = 160.0f;
    ride.bottom = 90.0f;
    ride.pad = 1;

    hvd::MouseTarget tile;    // 그림 밖으로 삐져나간 자리
    tile.left = 150.0f;
    tile.top = 50.0f;
    tile.right = 260.0f;
    tile.bottom = 120.0f;
    tile.pad = 3;

    targets.targets = { bell, ride, tile };

    const auto padAt = [&targets] (float x, float y)
    {
        const auto* target = targets.find (x, y);
        return target != nullptr ? static_cast<int> (target->pad) : -1;
    };

    CHECK (padAt (100.0f, 50.0f) == 2);
    CHECK (padAt (60.0f, 50.0f) == 1);
    CHECK (padAt (45.0f, 15.0f) == -1);      // 타원의 테두리 사각형 구석은 밖이다
    CHECK (padAt (155.0f, 55.0f) == 1);      // 타원 안이 먼저 나온다
    CHECK (padAt (190.0f, 95.0f) == 3);      // 사각형
    CHECK (padAt (230.0f, 95.0f) == -1);     // 그림 밖은 다른 부품의 자리다
    CHECK (padAt (190.0f, 100.0f) == -1);

    auto copy = targets;
    CHECK (copy == targets);
    copy.targets[1].chokes.push_back (2);
    CHECK (! (copy == targets));

    // 입력 스레드의 기록과 화면 스레드가 받은 클릭 맞춰 보기 (ms)
    CHECK (hvd::isSameClick (1000.0, 1003.0, 1003.0));      // 보통: 기록이 조금 앞선다
    CHECK (hvd::isSameClick (1000.0, 1300.0, 1290.0));      // 화면이 조금 바빴다
    CHECK (hvd::isSameClick (1000.0, 2500.0, 990.0));       // 화면이 1.5초 바빴다: 누른 때로 맞춘다
    CHECK (hvd::isSameClick (1000.0, 2500.0, 1015.0));      // 누른 때는 16ms 단위라 조금 어긋난다
    CHECK (! hvd::isSameClick (1000.0, 2500.0, 1100.0));    // 바쁜 동안 따로 누른 클릭
    CHECK (! hvd::isSameClick (1200.0, 1003.0, 1003.0));    // 받은 뒤에 친 다른 클릭
    CHECK (! hvd::isSameClick (400.0, 1003.0, 1003.0));     // 오래전 기록
}

void testChordAccent()
{
    hvd::MappingSpec spec;
    spec.chordAccentMs = 30.0f;
    spec.bindings.push_back (bind ("KeyA", "a", 0.3f));
    spec.bindings.push_back (bind ("KeyS", "a", 0.3f));

    Rig rig (spec);

    const auto press = [&rig] (hvd::KeyCode key, std::uint64_t timeMs)
    {
        rig.performer.keyDown (key, timeMs * msNs);
        const auto onsets = rig.play (64);
        return onsets.size() == 1 ? onsets[0].value : -1.0f;
    };

    CHECK (isNear (press (keyA, 1000), 0.3f));
    CHECK (isNear (press (keyS, 1010), 1.0f));    // 10ms 뒤 다른 키: 악센트
    CHECK (isNear (press (keyA, 1200), 0.3f));    // 간격이 벌어지면 평소대로
    CHECK (isNear (press (keyA, 1210), 0.3f));    // 같은 키를 다시 누른 것은 악센트가 아니다
}

void testTapTempo()
{
    hvd::MappingSpec spec;
    spec.bindings.push_back (bindAction ("F1", "tapTempo"));
    spec.bindings.push_back (bindAction ("F2", "metronome"));

    Rig rig (spec);
    rig.engine.setTempo (90.0f);

    rig.performer.keyDown (keyF1, 1000 * msNs);
    CHECK (isNear (rig.engine.getTempo(), 90.0f));    // 한 번으로는 바뀌지 않는다

    rig.performer.keyDown (keyF1, 1500 * msNs);
    rig.performer.keyDown (keyF1, 2000 * msNs);
    CHECK (isNear (rig.engine.getTempo(), 120.0f, 0.01f));

    rig.performer.keyDown (keyF1, 9000 * msNs);       // 오랜만에 누르면 새로 센다
    CHECK (isNear (rig.engine.getTempo(), 120.0f, 0.01f));

    rig.performer.keyDown (keyF1, 9400 * msNs);
    CHECK (isNear (rig.engine.getTempo(), 150.0f, 0.01f));

    CHECK (! rig.engine.isMetronomeOn());
    rig.performer.keyDown (0x3c, 9500 * msNs);        // F2
    CHECK (rig.engine.isMetronomeOn());
    rig.performer.keyDown (0x3c, 9600 * msNs);
    CHECK (! rig.engine.isMetronomeOn());
}

void testMetronome()
{
    hvd::Engine engine;
    engine.prepare (48000.0);
    engine.setTempo (120.0f);    // 한 박이 24000프레임
    engine.setBeatsPerBar (4);

    CHECK (findOnsets (render (engine, 1000)).empty());

    engine.setMetronomeOn (true);
    const auto block = render (engine, 48000);

    CHECK (engine.getBeatCount() == 2);
    CHECK (std::fabs (block.left[5]) > 0.001f);        // 첫 박의 클릭
    CHECK (block.left[12000] == 0.0f);                 // 클릭 사이는 조용하다
    CHECK (std::fabs (block.left[24005]) > 0.001f);    // 둘째 박의 클릭
    CHECK (std::fabs (block.left[5]) > std::fabs (block.left[24005]));    // 첫 박이 더 크다

    engine.setMetronomeOn (false);
    render (engine, 4000);
    CHECK (findOnsets (render (engine, 48000)).empty());
    CHECK (engine.getBeatCount() == 2);
}

void testHumanize()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (1.0f, 16));
    kit.pads.push_back (makePad ("a", 0));
    kit.humanizeVolume = 0.2f;
    kit.humanizePitchCents = 10.0f;

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);

    auto lowest = 10.0f, highest = 0.0f;

    for (int i = 0; i < 40; ++i)
    {
        hit (engine, 0);
        const auto value = render (engine, 32).left[1];
        lowest = std::min (lowest, value);
        highest = std::max (highest, value);
    }

    CHECK (lowest >= 0.79f && highest <= 1.21f);    // 범위 안에서
    CHECK (highest - lowest > 0.05f);               // 매번 조금씩 다르다
}

void testMappingChangeStopsRunners()
{
    hvd::MappingSpec spec;
    spec.bindings.push_back (bind ("KeyA", "a", 1.0f, "pattern"));
    spec.bindings.back().steps = "X";

    Rig rig (spec);
    rig.performer.keyDown (keyA, 1000 * msNs);
    rig.play (64);
    CHECK (rig.engine.getActiveRunnerCount() == 1);

    // 키 배열을 바꾼 뒤 첫 키 입력에서 켜 둔 패턴이 정리된다
    std::vector<std::string> warnings;
    rig.performer.setMapping (hvd::resolveMapping (spec, rig.kit, warnings));
    rig.performer.keyUp (keyD, 2000 * msNs);
    rig.play (64);
    CHECK (rig.engine.getActiveRunnerCount() == 0);

    // 키 배열이 없으면 아무 일도 하지 않는다
    rig.performer.setMapping (nullptr);
    rig.performer.keyDown (keyA, 3000 * msNs);
    CHECK (rig.play (64).empty());
}

void testRunnerLimit()
{
    auto kit = makeImpulseKit();

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);
    render (engine, 16);

    for (std::uint32_t id = 0; id < 40; ++id)
    {
        hvd::EngineCommand command;
        command.type = hvd::EngineCommand::Type::runnerStart;
        command.runnerId = id;
        command.pad = 0;
        command.runner.intervalSamples = 100000.0;
        engine.postFromInput (command);
    }

    render (engine, 64);
    CHECK (engine.getActiveRunnerCount() == hvd::Engine::maxRunners);
    CHECK (engine.getDroppedCommandCount() == 8);

    // 간격이 0이어도 오디오 스레드가 묶이지 않는다
    hvd::EngineCommand stopAll;
    stopAll.type = hvd::EngineCommand::Type::allRunnersStop;
    engine.postFromInput (stopAll);

    hvd::EngineCommand broken;
    broken.type = hvd::EngineCommand::Type::runnerStart;
    broken.runnerId = 99;
    broken.runner.intervalSamples = 0.0;
    engine.postFromInput (broken);

    render (engine, 4096);
    CHECK (engine.getActiveRunnerCount() == 1);
}

//==============================================================================
// M3 ~ M5: 패드 설정, 타격 기록, 이펙트, 반주 재생, 리플레이

void setPadParams (hvd::Engine& engine, int pad, const hvd::PadParams& params)
{
    hvd::EngineCommand command;
    command.type = hvd::EngineCommand::Type::setPadParams;
    command.pad = static_cast<std::uint16_t> (pad);
    command.padParams = params;
    engine.postFromControl (command);
}

void testPadParams()
{
    hvd::Kit kit;
    kit.samples.push_back (makeConstantSample (0.5f, 100));

    hvd::Sample ramp;
    ramp.left.resize (1000);

    for (std::size_t i = 0; i < ramp.left.size(); ++i)
        ramp.left[i] = static_cast<float> (i) * 0.001f;

    kit.samples.push_back (std::make_shared<const hvd::Sample> (ramp));
    kit.pads.push_back (makePad ("a", 0));
    kit.pads.push_back (makePad ("ramp", 1));
    kit.pads[0].volume = 0.5f;

    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);

    hit (engine, 0);
    CHECK (isNear (render (engine, 64).left[0], 0.25f));    // 킷에 적힌 음량에서 시작한다
    render (engine, 200);

    // 연주 중에 음량과 팬을 바꾼다
    hvd::PadParams params;
    params.volume = 2.0f;
    params.pan = 1.0f;
    setPadParams (engine, 0, params);
    hit (engine, 0);
    auto block = render (engine, 64);
    CHECK (block.left[0] == 0.0f);
    CHECK (isNear (block.right[0], 1.0f));
    render (engine, 200);

    // 뮤트한 패드는 소리가 나지 않지만 친 것으로는 센다
    params.mute = true;
    setPadParams (engine, 0, params);
    hit (engine, 0);
    block = render (engine, 64);
    CHECK (block.left[0] == 0.0f && block.right[0] == 0.0f);
    CHECK (engine.getHitCount (0) == 3);

    // 시작 지점: 샘플의 앞 100프레임을 건너뛴다
    hvd::PadParams skip;
    skip.startSeconds = 100.0f / 48000.0f;
    setPadParams (engine, 1, skip);
    hit (engine, 1);
    block = render (engine, 64);
    CHECK (isNear (block.left[0], 0.1f, 1.0e-3f));
    CHECK (isNear (block.left[10], 0.11f, 1.0e-3f));
    render (engine, 2000);

    // 시작 지점이 샘플보다 길면 소리가 없다
    skip.startSeconds = 1.0f;
    setPadParams (engine, 1, skip);
    hit (engine, 1);
    CHECK (render (engine, 64).left[0] == 0.0f);
    CHECK (engine.getActiveVoiceCount() == 0);

    // 킷을 다시 넘기면 킷에 적힌 값으로 돌아간다
    setKit (engine, kit);
    hit (engine, 0);
    CHECK (isNear (render (engine, 64).left[0], 0.25f));
}

void testFieldTables()
{
    // 패드 값: 이름으로 찾고, 한도와 기본값이 엔진의 기본값과 맞는다.
    for (const auto& field : hvd::padFields)
    {
        CHECK (hvd::findPadField (field.name) == &field);
        CHECK (field.minimum <= field.fallback && field.fallback <= field.maximum);
    }

    CHECK (hvd::findPadField ("nothing") == nullptr);
    CHECK (hvd::findPadField ("volume")->maximum == hvd::maxPadVolume);

    const hvd::Pad pad;
    CHECK (isNear (hvd::padField::volume.fallback, pad.volume) && isNear (hvd::padField::pan.fallback, pad.pan));
    CHECK (isNear (hvd::padField::reverbSend.fallback, pad.reverbSend) && isNear (hvd::padField::chokeFadeMs.fallback, pad.chokeFadeMs));
    CHECK (isNear (hvd::padField::velocityCurve.fallback, pad.velocityCurve));

    const auto big = hvd::makePadParams (20.0, -3.0, 30.0, 2.0, 6000.0, 5.0);
    CHECK (isNear (big.volume, hvd::maxPadVolume) && isNear (big.pan, -1.0f) && isNear (big.pitchRatio, 4.0f));
    CHECK (isNear (big.reverbSend, 1.0f) && isNear (big.startSeconds, 5.0f) && isNear (big.velocityCurve, 4.0f) && ! big.mute);

    const auto normal = hvd::makePadParams (0.5, 0.25, -12.0, 0.3, 80.0, 1.5);
    CHECK (isNear (normal.volume, 0.5f) && isNear (normal.pan, 0.25f) && isNear (normal.pitchRatio, 0.5f));
    CHECK (isNear (normal.reverbSend, 0.3f) && isNear (normal.startSeconds, 0.08f) && isNear (normal.velocityCurve, 1.5f));
    CHECK (isNear (hvd::makePadParams (1.0, 0.0, 0.0, 1.0, -5.0, -1.0).startSeconds, 0.0f));

    // 이펙트 값: 기본값이 FxSettings의 기본값과 같고, 값마다 다른 칸을 가리킨다.
    const hvd::FxSettings defaults;
    hvd::FxSettings marked;
    auto index = 1.0f;

    for (const auto& field : hvd::fxFields)
    {
        CHECK (isNear (defaults.*field.member, field.fallback));
        CHECK (field.minimum <= field.fallback && field.fallback <= field.maximum);
        marked.*field.member = index;
        index += 1.0f;
    }

    CHECK (marked.eqLowDb == 1.0f && marked.eqMidDb == 2.0f && marked.eqHighDb == 3.0f && marked.compAmount == 4.0f);
    CHECK (marked.reverbLevel == 5.0f && marked.reverbSize == 6.0f && marked.reverbDamp == 7.0f);

    CHECK (isNear (hvd::defaultGapMs ("double"), 85.0f) && isNear (hvd::defaultGapMs ("flam"), 28.0f) && isNear (hvd::defaultGapMs ("drag"), 55.0f));
    CHECK (hvd::defaultGapMs ("single") == 0.0f && hvd::defaultGapMs ("") == 0.0f);
}

void testHitEventsAndPeaks()
{
    auto kit = makeImpulseKit();
    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);
    render (engine, 16);
    CHECK (engine.getClock() == 16);

    hvd::HitEvent event;
    CHECK (! engine.popHitEvent (event));

    hit (engine, 1, 1.0f, 10);
    hit (engine, 0, 0.0f);    // 세기 0은 기록하지 않는다
    render (engine, 64);

    CHECK (engine.popHitEvent (event));
    CHECK (event.time == 26 && event.pad == 1 && isNear (event.velocity, 1.0f));
    CHECK (! engine.popHitEvent (event));

    CHECK (isNear (engine.takePadPeak (1), 0.5f));
    CHECK (engine.takePadPeak (1) == 0.0f);    // 읽으면 지워진다
    CHECK (engine.takePadPeak (0) == 0.0f);
    CHECK (engine.takePadPeak (-1) == 0.0f);
    CHECK (engine.getClock() == 80);
}

void testHitAtAndAux()
{
    auto kit = makeImpulseKit();
    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);
    render (engine, 16);

    // 엔진 시계 기준의 시각에 맞춰 친다
    hvd::EngineCommand command;
    command.type = hvd::EngineCommand::Type::hitAt;
    command.pad = 0;
    command.time = engine.getClock() + 100;
    engine.postFromControl (command);

    auto onsets = findOnsets (render (engine, 256));
    CHECK (onsets.size() == 1 && onsetIs (onsets, 0, 100, 1.0f));

    // 이미 지난 시각이면 바로 친다
    command.pad = 1;
    command.time = 5;
    engine.postFromControl (command);
    onsets = findOnsets (render (engine, 256));
    CHECK (onsets.size() == 1 && onsetIs (onsets, 0, 0, 0.5f));

    // 세 번째 스레드(MIDI)용 큐
    hvd::EngineCommand fromMidi;
    fromMidi.pad = 2;
    CHECK (engine.postFromAux (fromMidi));
    onsets = findOnsets (render (engine, 256));
    CHECK (onsets.size() == 1 && onsetIs (onsets, 0, 0, 0.25f));

    // 큰 블록은 안에서 나눠 처리해도 타이밍이 그대로다
    hit (engine, 0, 1.0f, 2500);
    hit (engine, 1, 1.0f, 1024);
    onsets = findOnsets (render (engine, 4096));
    CHECK (onsets.size() == 2 && onsetIs (onsets, 0, 1024, 0.5f) && onsetIs (onsets, 1, 2500, 1.0f));

    // 출력이 한 채널뿐이어도 소리가 난다
    hit (engine, 0);
    std::vector<float> mono (64, 0.0f);
    float* channels[] = { mono.data() };
    engine.render (channels, 1, 64);
    CHECK (isNear (mono[0], 1.0f));
}

void testHitOrigin()
{
    // 엔진이 알려 온 타격을 모두 꺼낸다. 한 블록 안에서는 시간순이 아닐 수 있다.
    const auto drain = [] (hvd::Engine& engine)
    {
        std::vector<hvd::HitEvent> events;
        hvd::HitEvent event;

        while (engine.popHitEvent (event))
            events.push_back (event);

        return events;
    };

    const auto countFrom = [] (const std::vector<hvd::HitEvent>& events, int pad, hvd::HitSource source, hvd::KeyCode key)
    {
        return std::count_if (events.begin(), events.end(), [=] (const hvd::HitEvent& event)
        {
            return event.pad == pad && event.origin.source == source && event.origin.key == key;
        });
    };

    // 지시에 적은 출처가 타격 알림까지 그대로 온다: 바로 치는 것, 늦춰 치는 것, 시각을 정해 치는 것
    {
        auto kit = makeImpulseKit();
        hvd::Engine engine;
        engine.prepare (48000.0);
        setKit (engine, kit);
        render (engine, 16);

        hvd::EngineCommand command;
        command.pad = 0;
        engine.postFromControl (command);    // 출처를 밝히지 않으면 화면에서 누른 것으로 본다

        command.pad = 1;
        command.delaySamples = 20;
        command.origin = { hvd::HitSource::midi, 0 };
        engine.postFromAux (command);

        command.type = hvd::EngineCommand::Type::hitAt;
        command.pad = 2;
        command.delaySamples = 0;
        command.time = engine.getClock() + 40;
        command.origin = { hvd::HitSource::autoplay, 0 };
        engine.postFromControl (command);
        render (engine, 64);

        const auto events = drain (engine);
        CHECK (events.size() == 3);
        CHECK (countFrom (events, 0, hvd::HitSource::screen, 0) == 1);
        CHECK (countFrom (events, 1, hvd::HitSource::midi, 0) == 1);
        CHECK (countFrom (events, 2, hvd::HitSource::autoplay, 0) == 1);
    }

    // 키로 친 것은 어느 키였는지가 함께 온다. 이어 치기와 뒤따라 치는 타격도 마찬가지다.
    {
        hvd::MappingSpec spec;
        spec.chordAccentMs = 0.0f;
        spec.bindings.push_back (bind ("KeyA", "a", 1.0f));
        spec.bindings.push_back (bind ("KeyS", "b", 1.0f, "repeat"));
        spec.bindings.back().intervalMs = 10.0f;
        spec.bindings.push_back (bind ("KeyD", "c", 1.0f, "double"));
        spec.bindings.back().gapMs = 10.0f;

        Rig rig (spec);
        rig.performer.keyDown (keyA, 1000 * msNs);
        rig.performer.keyDown (keyS, 1000 * msNs);
        rig.performer.keyDown (keyD, 1000 * msNs);
        rig.play (1000);

        auto events = drain (rig.engine);
        CHECK (events.size() == 6);
        CHECK (countFrom (events, 0, hvd::HitSource::key, keyA) == 1);
        CHECK (countFrom (events, 1, hvd::HitSource::key, keyS) == 3);    // 0, 480, 960프레임
        CHECK (countFrom (events, 2, hvd::HitSource::key, keyD) == 2);    // 첫 타와 10ms 뒤의 둘째 타

        rig.performer.keyUp (keyS, 1050 * msNs);
        rig.play (64);
        drain (rig.engine);

        // 버즈 롤이 뒤에 튀는 타격(엔진이 예약해 두는 것)도 누른 키를 달고 온다.
        hvd::MappingSpec buzz;
        buzz.bindings.push_back (bind ("KeyA", "a", 1.0f, "buzz"));
        buzz.bindings.back().intervalMs = 50.0f;

        Rig buzzRig (buzz);
        buzzRig.performer.keyDown (keyA, 1000 * msNs);
        buzzRig.play (2300);
        events = drain (buzzRig.engine);
        CHECK (events.size() == 3);
        CHECK (countFrom (events, 0, hvd::HitSource::key, keyA) == 3);
    }

    // 다시 재생하는 타격은 리플레이로 표시된다.
    {
        auto kit = makeImpulseKit();
        hvd::Engine engine;
        engine.prepare (48000.0);
        setKit (engine, kit);
        render (engine, 16);

        hvd::ReplayPlayer replay;
        replay.start ({ { 0.001, 1, 0.5f } }, engine.getClock(), 48000.0);
        replay.pump (engine, engine.getClock(), 4800);
        render (engine, 256);

        const auto events = drain (engine);
        CHECK (events.size() == 1);
        CHECK (countFrom (events, 1, hvd::HitSource::replay, 0) == 1);
    }
}

void testEffects()
{
    // 저음 셸프 +6dB: 변하지 않는 신호(0 Hz)가 약 2배가 된다
    {
        hvd::Kit kit;
        kit.samples.push_back (makeConstantSample (0.25f, 48000));
        kit.pads.push_back (makePad ("a", 0));

        hvd::Engine engine;
        engine.prepare (48000.0);
        setKit (engine, kit);

        hvd::FxSettings settings;
        settings.eqLowDb = 6.0f;
        engine.setFxSettings (settings);
        CHECK (isNear (engine.getFxSettings().eqLowDb, 6.0f));

        hit (engine, 0);
        const auto block = render (engine, 24000);
        CHECK (isNear (block.left[20000], 0.25f * 1.9953f, 0.005f));

        // 중음과 고음을 올려도 0 Hz 신호는 그대로다
        settings.eqLowDb = 0.0f;
        settings.eqMidDb = 9.0f;
        settings.eqHighDb = 9.0f;
        engine.setFxSettings (settings);
        const auto next = render (engine, 20000);
        CHECK (isNear (next.left[19000], 0.25f, 0.005f));
    }

    // 컴프레서: 큰 신호를 줄이고, 줄인 양을 알려 준다
    {
        hvd::Kit kit;
        kit.samples.push_back (makeConstantSample (1.0f, 48000));
        kit.pads.push_back (makePad ("a", 0));

        hvd::Engine engine;
        engine.prepare (48000.0);
        setKit (engine, kit);

        hvd::FxSettings settings;
        settings.compAmount = 1.0f;    // 문턱 -24dB, 비율 4:1 → 18dB 줄이고 9dB 보상
        engine.setFxSettings (settings);

        hit (engine, 0);
        const auto block = render (engine, 24000);
        CHECK (isNear (block.left[20000], 0.3548f, 0.01f));
        CHECK (isNear (block.right[20000], 0.3548f, 0.01f));
        CHECK (isNear (engine.getGainReductionDb(), 18.0f, 0.5f));
    }

    // 리버브: 소리가 끝난 뒤에도 잔향이 남고, 보내는 양이 0인 패드에는 걸리지 않는다
    {
        auto kit = makeImpulseKit();
        hvd::Engine engine;
        engine.prepare (48000.0);
        setKit (engine, kit);

        const auto tailEnergy = [&engine]
        {
            const auto block = render (engine, 24000);
            auto sum = 0.0;

            for (std::size_t i = 4000; i < block.left.size(); ++i)
                sum += std::fabs (block.left[i]) + std::fabs (block.right[i]);

            return sum;
        };

        hit (engine, 0);
        CHECK (tailEnergy() == 0.0);    // 리버브가 꺼져 있으면 임펄스뿐이다

        hvd::FxSettings settings;
        settings.reverbLevel = 1.0f;
        engine.setFxSettings (settings);

        hvd::PadParams dry;
        dry.reverbSend = 0.0f;
        setPadParams (engine, 1, dry);

        hit (engine, 1);
        CHECK (tailEnergy() == 0.0);    // 보내는 양이 0인 패드

        hit (engine, 0);
        CHECK (tailEnergy() > 0.01);

        settings.reverbLevel = 0.0f;
        engine.setFxSettings (settings);
        CHECK (tailEnergy() == 0.0);    // 끄면 바로 조용해진다
    }
}

void testPeakLimiter()
{
    constexpr auto ceiling = hvd::PeakLimiter::ceiling;
    constexpr auto sampleRate = 48000.0;
    const auto holdFrames = static_cast<std::size_t> (hvd::PeakLimiter::holdSeconds * sampleRate + 0.5);
    const auto releaseFrames = static_cast<std::size_t> (hvd::PeakLimiter::releaseSeconds * sampleRate + 0.5);

    // 천장 아래의 신호는 손대지 않는다
    {
        hvd::PeakLimiter limiter;
        limiter.prepare (sampleRate);

        std::vector<float> left (2000), right (2000);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            left[i] = 0.8f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));
            right[i] = -0.4f * static_cast<float> (std::sin (0.031 * static_cast<double> (i)));
        }

        const auto originalLeft = left;
        const auto originalRight = right;
        limiter.process (left.data(), right.data(), static_cast<int> (left.size()));

        CHECK (left == originalLeft && right == originalRight);
        CHECK (limiter.getGain() == 1.0f);
    }

    // 넘는 샘플은 바로 그 샘플에서 천장에 맞춘다(지연이 없다). 좌우를 같은 비율로 줄이고,
    // 잠깐 붙잡아 둔 뒤 되돌린다
    {
        hvd::PeakLimiter limiter;
        limiter.prepare (sampleRate);

        const std::size_t spike = 100;
        const auto total = spike + holdFrames + 12 * releaseFrames;
        std::vector<float> left (total, 0.3f), right (total, -0.15f);
        left[spike] = 1.8f;    // 천장의 두 배
        right[spike] = -0.9f;

        limiter.process (left.data(), right.data(), static_cast<int> (left.size()));

        CHECK (left[spike - 1] == 0.3f && right[spike - 1] == -0.15f);    // 앞의 샘플은 그대로
        CHECK (isNear (left[spike], ceiling));                            // 넘은 샘플이 바로 천장에 맞는다
        CHECK (isNear (right[spike], -0.5f * ceiling));                   // 오른쪽도 똑같이 절반으로
        CHECK (isNear (left[spike + 1], 0.15f) && isNear (right[spike + 1], -0.075f));

        // 붙잡아 두는 동안에는 절반 그대로이고, 그 뒤부터 되돌아온다
        CHECK (isNear (left[spike + holdFrames], 0.15f));
        CHECK (left[spike + holdFrames + 1] > left[spike + holdFrames]);

        // 되돌리는 시간 상수만큼 지나면 줄인 양이 1/e로 준다 (게인 0.5 → 1 - 0.5/e)
        const auto expected = 0.3f * (1.0f - 0.5f * std::exp (-1.0f));
        CHECK (isNear (left[spike + holdFrames + releaseFrames], expected, 0.001f));

        // 한참 뒤에는 완전히 돌아와, 다시 손대지 않은 그대로 나간다
        CHECK (left.back() == 0.3f && right.back() == -0.15f);
        CHECK (limiter.getGain() == 1.0f);
    }

    // 계속 큰 신호(저음에 고음이 실린 것)도 천장을 넘지 않고 좌우의 비율이 그대로다.
    // 장치 버퍼처럼 조각내 처리해도 한 번에 처리한 것과 같다
    {
        std::vector<float> left (9600), right (9600);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const auto seconds = static_cast<double> (i) / sampleRate;
            const auto value = 2.0 * std::sin (6.283185307179586 * 60.0 * seconds)
                             + 0.4 * std::sin (6.283185307179586 * 7000.0 * seconds);
            left[i] = static_cast<float> (value);
            right[i] = static_cast<float> (0.5 * value);
        }

        auto wholeLeft = left;
        auto wholeRight = right;

        hvd::PeakLimiter whole;
        whole.prepare (sampleRate);
        whole.process (wholeLeft.data(), wholeRight.data(), static_cast<int> (wholeLeft.size()));

        hvd::PeakLimiter chunked;
        chunked.prepare (sampleRate);

        for (std::size_t done = 0; done < left.size(); done += 128)
            chunked.process (left.data() + done, right.data() + done,
                             static_cast<int> (std::min<std::size_t> (128, left.size() - done)));

        auto peak = 0.0f;
        auto sameAsWhole = true;
        auto ratioKept = true;

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            peak = std::max ({ peak, std::fabs (left[i]), std::fabs (right[i]) });
            sameAsWhole = sameAsWhole && isNear (left[i], wholeLeft[i], 1.0e-6f) && isNear (right[i], wholeRight[i], 1.0e-6f);
            ratioKept = ratioKept && isNear (right[i], 0.5f * left[i]);
        }

        CHECK (peak <= ceiling + 1.0e-5f);
        CHECK (peak > ceiling - 1.0e-3f);    // 필요한 만큼만 줄인다
        CHECK (sameAsWhole);
        CHECK (ratioKept);
        CHECK (chunked.getGain() < 1.0f);    // 아직 줄이는 중이다
    }

    // 샘플레이트가 바뀌면 시간도 따라간다
    {
        const auto holdFramesAt96k = static_cast<std::size_t> (hvd::PeakLimiter::holdSeconds * 96000.0 + 0.5);

        hvd::PeakLimiter limiter;
        limiter.prepare (96000.0);

        std::vector<float> left (holdFramesAt96k + 100, 0.3f), right (holdFramesAt96k + 100, 0.3f);
        left[0] = 1.8f;
        limiter.process (left.data(), right.data(), static_cast<int> (left.size()));

        CHECK (isNear (left[holdFramesAt96k], 0.15f));
        CHECK (left[holdFramesAt96k + 1] > left[holdFramesAt96k]);
    }

    // reset()은 줄이던 것을 바로 푼다
    {
        hvd::PeakLimiter limiter;
        limiter.prepare (sampleRate);

        float left[] = { 3.0f, 0.3f };
        float right[] = { 3.0f, 0.3f };
        limiter.process (left, right, 2);
        CHECK (isNear (left[0], ceiling) && isNear (left[1], 0.1f * ceiling));
        CHECK (isNear (limiter.getGain(), ceiling / 3.0f));

        limiter.reset();
        CHECK (limiter.getGain() == 1.0f);

        float nextLeft[] = { 0.3f };
        float nextRight[] = { -0.3f };
        limiter.process (nextLeft, nextRight, 1);
        CHECK (nextLeft[0] == 0.3f && nextRight[0] == -0.3f);
    }
}

//==============================================================================
hvd::TrackData makeSineTrack (double frequency, double seconds, double sampleRate)
{
    hvd::TrackData track;
    track.sampleRate = sampleRate;

    const auto frames = static_cast<std::size_t> (seconds * sampleRate);
    track.samples.resize (frames * 2);

    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto value = 16000.0 * std::sin (6.283185307179586 * frequency * static_cast<double> (i) / sampleRate);
        track.samples[i * 2] = static_cast<std::int16_t> (value);
        track.samples[i * 2 + 1] = static_cast<std::int16_t> (value * 0.5);
    }

    return track;
}

Block renderTrack (hvd::TrackPlayer& player, int numFrames, int blockSize = 256)
{
    Block block;
    block.left.assign (static_cast<std::size_t> (numFrames), 0.0f);
    block.right.assign (static_cast<std::size_t> (numFrames), 0.0f);

    for (int done = 0; done < numFrames; done += blockSize)
        player.render (block.left.data() + done, block.right.data() + done, std::min (blockSize, numFrames - done));

    return block;
}

int countRisingCrossings (const std::vector<float>& signal, std::size_t begin, std::size_t end)
{
    auto count = 0;

    for (auto i = begin + 1; i < end; ++i)
        if (signal[i - 1] < 0.0f && signal[i] >= 0.0f)
            ++count;

    return count;
}

float peakBetween (const std::vector<float>& signal, std::size_t begin, std::size_t end)
{
    auto peak = 0.0f;

    for (auto i = begin; i < end; ++i)
        peak = std::max (peak, std::fabs (signal[i]));

    return peak;
}

void testTrackPlayback()
{
    const auto track = makeSineTrack (440.0, 1.0, 48000.0);

    hvd::TrackPlayer player;
    player.prepare (48000.0);
    player.setGain (1.0f);

    // 곡이 없으면 조용하다
    player.setPlaying (true);
    CHECK (peakBetween (renderTrack (player, 512).left, 0, 512) == 0.0f);

    player.setPlaying (false);
    CHECK (player.setTrack (&track));
    CHECK (peakBetween (renderTrack (player, 512).left, 0, 512) == 0.0f);    // 멈춰 있으면 조용하다
    CHECK (isNear (static_cast<float> (player.getLength()), 1.0f, 1.0e-3f));

    player.setPlaying (true);
    const auto block = renderTrack (player, 57600);

    // 원래 속도에서는 원본과 똑같은 소리가 난다 (처음 잠깐 커지는 구간 뒤)
    auto same = true;

    for (std::size_t i = 2000; i < 46000; i += 37)
    {
        const auto expected = static_cast<float> (track.samples[i * 2]) / 32768.0f;
        same = same && isNear (block.left[i], expected, 1.0e-4f)
                    && isNear (block.right[i], static_cast<float> (track.samples[i * 2 + 1]) / 32768.0f, 1.0e-4f);
    }

    CHECK (same);

    // 끝까지 재생하면 멈추고 처음으로 돌아간다
    CHECK (! player.isPlaying());
    CHECK (player.getPosition() == 0.0);
    CHECK (peakBetween (block.left, 50000, 57600) == 0.0f);

    // 곡을 바꾸면 이전 곡을 돌려준다
    const auto other = makeSineTrack (220.0, 0.5, 48000.0);
    CHECK (player.popRetiredTrack() == nullptr);
    player.setTrack (&other);
    renderTrack (player, 256);
    CHECK (player.popRetiredTrack() == &track);
    CHECK (isNear (static_cast<float> (player.getLength()), 0.5f, 1.0e-3f));

    // 재생하던 곡을 닫으면 "진행 중" 표시도 꺼진다 (켜진 채로 남으면 곡이 없는데도 재생 중으로 보인다)
    player.setPlaying (true);
    renderTrack (player, 256);
    CHECK (player.wasRenderActive());

    player.setPlaying (false);
    player.setTrack (nullptr);
    renderTrack (player, 256);
    CHECK (player.popRetiredTrack() == &other);
    CHECK (player.getLength() == 0.0);
    CHECK (! player.wasRenderActive());
}

void testTrackSpeed()
{
    const auto track = makeSineTrack (440.0, 3.0, 48000.0);

    // 절반 속도: 진행은 절반, 음높이는 그대로
    {
        hvd::TrackPlayer player;
        player.prepare (48000.0);
        player.setGain (1.0f);
        player.setTrack (&track);
        player.setSpeed (0.5f);
        player.setPlaying (true);

        const auto block = renderTrack (player, 48000);
        CHECK (std::fabs (player.getPosition() - 0.5) < 0.06);

        const auto crossings = countRisingCrossings (block.left, 4800, 43200);    // 0.8초 → 352번
        CHECK (crossings >= 342 && crossings <= 362);

        const auto peak = peakBetween (block.left, 4800, 43200);
        CHECK (peak > 0.35f && peak < 0.62f);
    }

    // 1.5배 속도
    {
        hvd::TrackPlayer player;
        player.prepare (48000.0);
        player.setGain (1.0f);
        player.setTrack (&track);
        player.setSpeed (1.5f);
        player.setPlaying (true);

        const auto block = renderTrack (player, 24000);
        CHECK (std::fabs (player.getPosition() - 0.75) < 0.06);

        const auto crossings = countRisingCrossings (block.left, 2400, 21600);    // 0.4초 → 176번
        CHECK (crossings >= 169 && crossings <= 183);
    }

    // 속도는 0.5 ~ 1.5로 잘린다
    hvd::TrackPlayer limits;
    limits.setSpeed (9.0f);
    CHECK (isNear (limits.getSpeed(), 1.5f));
    limits.setSpeed (0.0f);
    CHECK (isNear (limits.getSpeed(), 0.5f));
}

void testTrackLoopAndSeek()
{
    const auto track = makeSineTrack (330.0, 4.0, 48000.0);

    hvd::TrackPlayer player;
    player.prepare (48000.0);
    player.setGain (1.0f);
    player.setTrack (&track);
    player.setLoop (1.0, 2.0, true);
    player.seek (1.5);
    player.setPlaying (true);

    // 3초 동안 재생해도 1 ~ 2초 구간 안에 머문다
    auto inside = true;
    auto wrapped = false;
    auto previous = 1.5;

    for (int i = 0; i < 30; ++i)
    {
        renderTrack (player, 4800);
        const auto now = player.getPosition();
        inside = inside && now > 0.95 && now < 2.06;
        wrapped = wrapped || now < previous;
        previous = now;
    }

    CHECK (inside);
    CHECK (wrapped);
    CHECK (player.isPlaying());

    // 반복을 끄고 다른 곳으로 옮긴다
    player.setLoop (0.0, 0.0, false);
    player.seek (3.0);
    renderTrack (player, 4800);
    CHECK (std::fabs (player.getPosition() - 3.1) < 0.06);

    // 멈추면 소리가 사라지고 위치가 그대로다
    player.setPlaying (false);
    renderTrack (player, 2000);
    const auto paused = player.getPosition();
    CHECK (peakBetween (renderTrack (player, 1000).left, 0, 1000) == 0.0f);
    CHECK (player.getPosition() == paused);
}

void testTrackResample()
{
    // 44.1 kHz 곡을 48 kHz 장치에서 재생해도 빠르기와 음높이가 같다
    const auto track = makeSineTrack (441.0, 2.0, 44100.0);

    hvd::TrackPlayer player;
    player.prepare (48000.0);
    player.setGain (1.0f);
    player.setTrack (&track);
    player.setPlaying (true);

    const auto block = renderTrack (player, 24000, 128);
    CHECK (std::fabs (player.getPosition() - 0.5) < 0.06);

    const auto crossings = countRisingCrossings (block.left, 4800, 19200);    // 0.3초 → 132번
    CHECK (crossings >= 130 && crossings <= 134);

    const auto peak = peakBetween (block.left, 4800, 19200);
    CHECK (peak > 0.47f && peak < 0.51f);
}

void testReplay()
{
    auto kit = makeImpulseKit();
    hvd::Engine engine;
    engine.prepare (48000.0);
    setKit (engine, kit);
    render (engine, 16);

    hvd::ReplayPlayer replay;
    CHECK (! replay.isActive());
    CHECK (! replay.pump (engine, engine.getClock(), 4800));

    // 순서가 뒤섞여 있어도 시간순으로 친다
    std::vector<hvd::ReplayEvent> events;
    events.push_back ({ 0.02, 1, 0.5f });
    events.push_back ({ 0.01, 0, 1.0f });
    events.push_back ({ 1.0, 2, 1.0f });

    replay.start (events, engine.getClock(), 48000.0);
    CHECK (isNear (static_cast<float> (replay.getLengthSeconds()), 1.0f));

    // 0.1초 앞까지만 미리 넘긴다
    CHECK (replay.pump (engine, engine.getClock(), 4800));
    auto onsets = findOnsets (render (engine, 4096));
    CHECK (onsets.size() == 2 && onsetIs (onsets, 0, 480, 1.0f) && onsetIs (onsets, 1, 960, 0.25f));

    // 나머지는 때가 가까워지면 넘긴다
    render (engine, 40000);
    CHECK (replay.pump (engine, engine.getClock(), 4800));    // 1초 지점의 타격이 0.1초 안에 들어왔다
    onsets = findOnsets (render (engine, 8192));
    CHECK (onsets.size() == 1 && onsetIs (onsets, 0, 48000 - 44096, 0.25f));

    CHECK (! replay.pump (engine, engine.getClock(), 4800));  // 다 쳤다
    CHECK (! replay.isActive());
}

void testTrackLeadIn()
{
    // 리듬 게임의 준비 시간: 곡의 0초보다 앞에서 시작하면 그동안 무음이 나오고, 위치는 음수에서 0을 지나 이어진다.
    const auto track = makeSineTrack (440.0, 2.0, 48000.0);

    hvd::TrackPlayer player;
    player.prepare (48000.0);
    player.setGain (1.0f);
    CHECK (player.setTrack (&track));

    player.seekWithLeadIn (-0.5);
    player.setPlaying (true);

    auto block = renderTrack (player, 12000);    // 0.25초
    CHECK (peakBetween (block.left, 0, 12000) == 0.0f);
    CHECK (player.wasRenderActive());
    CHECK (player.getRenderStartPosition() < -0.24 && player.getRenderStartPosition() > -0.27);
    CHECK (player.getPosition() >= 0.0);    // 화면에 보이는 위치는 음수가 되지 않는다

    block = renderTrack (player, 24000);         // 0.25 ~ 0.75초: 곡의 0초를 지난다
    CHECK (peakBetween (block.left, 0, 11000) == 0.0f);
    CHECK (peakBetween (block.left, 13000, 24000) > 0.4f);

    // 블록의 첫 샘플이 곡의 어디였는지 샘플 단위로 맞는다.
    renderTrack (player, 256);
    CHECK (std::abs (player.getRenderStartPosition() - 0.25) < 1.0e-4);

    // 절반 속도에서는 곡 안의 위치가 절반의 빠르기로 간다.
    player.setSpeed (0.5f);
    player.seekWithLeadIn (0.5);
    renderTrack (player, 256);
    const auto before = player.getRenderStartPosition();
    renderTrack (player, 48000 - 256);
    renderTrack (player, 256);
    const auto after = player.getRenderStartPosition();
    CHECK (std::abs (before - 0.5) < 0.01);
    CHECK (std::abs ((after - before) - 0.5) < 0.03);

    // 준비 시간이 있는 이동은 만들어 둔 소리를 버리고 바로 새 자리에서 시작한다.
    player.setSpeed (1.0f);
    player.seekWithLeadIn (-0.1);
    block = renderTrack (player, 2400);
    CHECK (peakBetween (block.left, 0, 2400) == 0.0f);

    player.detachTrack();
}

//==============================================================================
// 리듬 게임: 레인, 채보, 판정, 드럼 받아 적기

void testLanes()
{
    hvd::Lane lane = hvd::Lane::crash;

    CHECK (hvd::laneForPadId ("kick", lane) && lane == hvd::Lane::kick);
    CHECK (hvd::laneForPadId ("snare_rim", lane) && lane == hvd::Lane::snare);
    CHECK (hvd::laneForPadId ("sidestick", lane) && lane == hvd::Lane::snare);
    CHECK (hvd::laneForPadId ("hihat_open", lane) && lane == hvd::Lane::hihat);
    CHECK (hvd::laneForPadId ("tom_high", lane) && lane == hvd::Lane::tomHigh);
    CHECK (hvd::laneForPadId ("tom_mid", lane) && lane == hvd::Lane::tomMid);
    CHECK (hvd::laneForPadId ("tom_floor", lane) && lane == hvd::Lane::tomFloor);
    CHECK (hvd::laneForPadId ("tom_low", lane) && lane == hvd::Lane::tomFloor);
    CHECK (hvd::laneForPadId ("ride_bell", lane) && lane == hvd::Lane::ride);
    CHECK (hvd::laneForPadId ("china", lane) && lane == hvd::Lane::crash);
    CHECK (hvd::laneForPadId ("Crash_Trash", lane) && lane == hvd::Lane::crash);
    CHECK (! hvd::laneForPadId ("cowbell", lane));

    for (int i = 0; i < hvd::numLanes; ++i)
    {
        hvd::Lane parsed = hvd::Lane::crash;
        CHECK (hvd::laneFromName (hvd::laneName (static_cast<hvd::Lane> (i)), parsed) && static_cast<int> (parsed) == i);
    }

    CHECK (! hvd::laneFromName ("nothing", lane));
}

void testPadKinds()
{
    using hvd::Instrument;
    using hvd::instrumentOfPad;

    CHECK (instrumentOfPad ("kick") == Instrument::kick);
    CHECK (instrumentOfPad ("Bass_Drum") == Instrument::kick);
    CHECK (instrumentOfPad ("bd2") == Instrument::kick);
    CHECK (instrumentOfPad ("snare_rim") == Instrument::snare);
    CHECK (instrumentOfPad ("snare_buzz") == Instrument::snare);
    CHECK (instrumentOfPad ("sidestick") == Instrument::snare);
    CHECK (instrumentOfPad ("rim") == Instrument::snare);
    CHECK (instrumentOfPad ("clap") == Instrument::snare);
    CHECK (instrumentOfPad ("sd") == Instrument::snare);
    CHECK (instrumentOfPad ("hihat_splash") == Instrument::hihat);
    CHECK (instrumentOfPad ("hat_open") == Instrument::hihat);
    CHECK (instrumentOfPad ("HH") == Instrument::hihat);
    CHECK (instrumentOfPad ("tom_floor") == Instrument::tom);
    CHECK (instrumentOfPad ("ride_bell") == Instrument::ride);
    CHECK (instrumentOfPad ("crash_trash") == Instrument::crash);
    CHECK (instrumentOfPad ("splash") == Instrument::crash);
    CHECK (instrumentOfPad ("cymbal_fx") == Instrument::crash);
    CHECK (instrumentOfPad ("China") == Instrument::china);
    CHECK (instrumentOfPad ("cowbell") == Instrument::other);
    CHECK (instrumentOfPad ("") == Instrument::other);
    CHECK (instrumentOfPad ("pad_1") == Instrument::other);

    CHECK (hvd::isCymbal (Instrument::hihat) && hvd::isCymbal (Instrument::ride) && hvd::isCymbal (Instrument::crash) && hvd::isCymbal (Instrument::china));
    CHECK (! hvd::isCymbal (Instrument::kick) && ! hvd::isCymbal (Instrument::snare) && ! hvd::isCymbal (Instrument::tom) && ! hvd::isCymbal (Instrument::other));

    CHECK (hvd::padFamily ("ride_bell") == "ride");
    CHECK (hvd::padFamily ("Ride_Edge") == "ride");
    CHECK (hvd::padFamily ("snare_rim") == "snare");
    CHECK (hvd::padFamily ("snare_buzz") == "snare");
    CHECK (hvd::padFamily ("sidestick") == "snare");
    CHECK (hvd::padFamily ("hihat_closed") == "hihat");
    CHECK (hvd::padFamily ("hihat_splash") == "hihat");
    CHECK (hvd::padFamily ("crash1") == "crash1");
    CHECK (hvd::padFamily ("tom_floor") == "tom_floor");
    CHECK (hvd::padFamily ("_bell") == "");

    CHECK (isNear (hvd::hihatOpenness ("hihat_closed"), 0.0f));
    CHECK (isNear (hvd::hihatOpenness ("hihat_half"), 0.45f));
    CHECK (isNear (hvd::hihatOpenness ("HiHat_Open"), 1.0f));
    CHECK (isNear (hvd::hihatOpenness ("hihat_pedal"), 0.0f));
    CHECK (isNear (hvd::hihatOpenness ("crash_open"), 0.0f));

    CHECK (hvd::startsWithIgnoringCase ("Crash1", "crash") && ! hvd::startsWithIgnoringCase ("cr", "crash"));
    CHECK (hvd::containsIgnoringCase ("Tom_Floor", "floor") && hvd::containsIgnoringCase ("x", "") && ! hvd::containsIgnoringCase ("tom", "low"));

    // 같은 악기와 같은 초크 그룹의 패드를 함께 멈춘다.
    hvd::Kit kit;

    for (const auto* id : { "ride", "ride_bell", "crash1", "hihat_closed", "hihat_open", "snare", "sidestick", "snare_rim", "kick", "tom_floor", "splash" })
        kit.pads.push_back (makePad (id, 0));

    kit.pads[3].group = kit.pads[4].group = 0;     // 하이햇 둘
    kit.pads[2].group = kit.pads[10].group = 1;    // 크래시와 스플래시

    const auto sets = hvd::chokeSetsOf (kit);
    using Set = std::vector<std::uint16_t>;

    CHECK (sets.size() == kit.pads.size());
    CHECK (sets[0] == Set ({ 0, 1 }) && sets[1] == Set ({ 0, 1 }));
    CHECK (sets[2] == Set ({ 2, 10 }) && sets[10] == Set ({ 2, 10 }));
    CHECK (sets[3] == Set ({ 3, 4 }) && sets[4] == Set ({ 3, 4 }));
    CHECK (sets[5] == Set ({ 5, 6, 7 }) && sets[6] == Set ({ 5, 6, 7 }) && sets[7] == Set ({ 5, 6, 7 }));
    CHECK (sets[8] == Set ({ 8 }) && sets[9] == Set ({ 9 }));
}

void testStandardPads()
{
    std::array<int, hvd::numLanes> preferred {};

    for (const auto& pad : hvd::standardPads)
    {
        hvd::Lane lane = hvd::Lane::kick;
        CHECK (hvd::laneForPadId (pad.id, lane) && lane == pad.lane);
        CHECK (pad.midiNote >= 0 && pad.midiNote < 128);
        CHECK (pad.x > 0.0f && pad.x < 1.0f && pad.y > 0.0f && pad.y < 1.0f && pad.size > 0.05f && pad.size < 0.6f);
        CHECK (hvd::findStandardPad (pad.id) == &pad);

        if (pad.preferred)
            ++preferred[static_cast<std::size_t> (pad.lane)];
    }

    // 레인마다 대표 패드가 하나씩 있다.
    for (int i = 0; i < hvd::numLanes; ++i)
    {
        CHECK (preferred[static_cast<std::size_t> (i)] == 1);
        CHECK (std::string_view (hvd::preferredPadOf (static_cast<hvd::Lane> (i))).size() > 0);
    }

    CHECK (std::string_view (hvd::preferredPadOf (hvd::Lane::crash)) == "crash1");
    CHECK (std::string_view (hvd::preferredPadOf (hvd::Lane::hihat)) == "hihat_closed");
    CHECK (hvd::findStandardPad ("kick")->midiNote == 36 && hvd::findStandardPad ("hihat_half")->midiNote == 23);
    CHECK (hvd::findStandardPad ("hihat_splash") == nullptr && hvd::findStandardPad ("") == nullptr);
}

hvd::ChartNote makeNote (double seconds, hvd::Lane lane, float velocity = 0.8f, double beat = -1.0)
{
    hvd::ChartNote note;
    note.seconds = seconds;
    note.lane = lane;
    note.velocity = velocity;
    note.beat = beat;
    return note;
}

void testRhythmGame()
{
    using hvd::Judgement;
    using hvd::Lane;

    hvd::RhythmGame game;
    const hvd::JudgeWindows windows;    // 50, 100, 160 ms

    game.start ({ makeNote (1.0, Lane::snare), makeNote (1.0, Lane::kick), makeNote (1.5, Lane::hihat),
                  makeNote (2.0, Lane::hihat), makeNote (2.5, Lane::tomHigh), makeNote (3.0, Lane::snare) },
                windows, 1.0, false);

    CHECK (! game.isFinished());
    CHECK (game.getAccuracy() == 1.0);

    // 노트가 없는 곳을 치면 아무 일도 없다.
    CHECK (game.hit (Lane::snare, 0.5).judgement == Judgement::pending);
    CHECK (game.getCombo() == 0 && game.getJudgedCount() == 0);

    auto result = game.hit (Lane::snare, 1.02);
    CHECK (result.judgement == Judgement::perfect && result.note == 0);
    CHECK (std::abs (result.offsetSeconds - 0.02) < 1.0e-9);

    result = game.hit (Lane::kick, 0.92);
    CHECK (result.judgement == Judgement::great && result.offsetSeconds < 0.0);

    // 같은 노트를 두 번 맞힐 수는 없다.
    CHECK (game.hit (Lane::snare, 1.03).judgement == Judgement::pending);

    result = game.hit (Lane::hihat, 1.63);
    CHECK (result.judgement == Judgement::good);
    CHECK (game.getCombo() == 3 && game.getMaxCombo() == 3);

    // 2.0초의 하이햇을 치지 않고 지나간다.
    CHECK (game.advance (2.1) == 0);
    CHECK (game.advance (2.2) == 1);
    CHECK (game.getCount (Judgement::miss) == 1 && game.getCombo() == 0);

    // 레인을 엄격하게 볼 때는 다른 탐으로 칠 수 없다.
    CHECK (game.hit (Lane::tomFloor, 2.5).judgement == Judgement::pending);
    CHECK (game.hit (Lane::tomHigh, 2.5).judgement == Judgement::perfect);

    CHECK (game.advance (10.0) == 1);
    CHECK (game.isFinished());
    CHECK (game.getJudgedCount() == 6);
    CHECK (game.getCount (Judgement::perfect) == 2 && game.getCount (Judgement::great) == 1 && game.getCount (Judgement::good) == 1);
    CHECK (game.getScore() > 0 && game.getScore() < 1000000);
    CHECK (game.getGrade() == 'D');
    CHECK (game.getEarlyCount() == 1 && game.getLateCount() == 1);

    // 모두 정확하게 치면 만점과 S.
    game.start ({ makeNote (1.0, Lane::snare), makeNote (2.0, Lane::kick) }, windows, 1.0, false);
    game.hit (Lane::snare, 1.0);
    game.hit (Lane::kick, 2.0);
    CHECK (game.getScore() == 1000000 && game.getGrade() == 'S' && game.isFinished());

    // 비슷한 악기 허용: 탐끼리, 심벌끼리는 바꿔 쳐도 된다. 같은 레인의 노트가 있으면 그것을 먼저 고른다.
    game.start ({ makeNote (1.0, Lane::tomHigh), makeNote (1.02, Lane::tomFloor), makeNote (2.0, Lane::ride), makeNote (3.0, Lane::snare) },
                windows, 1.0, true);
    CHECK (game.hit (Lane::tomFloor, 1.0).note == 1);
    CHECK (game.hit (Lane::tomMid, 1.0).note == 0);
    CHECK (game.hit (Lane::hihat, 2.0).judgement == Judgement::perfect);
    CHECK (game.hit (Lane::tomHigh, 3.0).judgement == Judgement::pending);

    // 절반 속도로 틀면 곡 안에서의 판정 구간도 절반이 된다 (실제 시간으로는 같다).
    game.start ({ makeNote (1.0, Lane::snare), makeNote (2.0, Lane::snare) }, windows, 0.5, false);
    result = game.hit (Lane::snare, 1.04);    // 실제 시간으로 80ms 늦음
    CHECK (result.judgement == Judgement::great && std::abs (result.offsetSeconds - 0.08) < 1.0e-9);
    CHECK (game.hit (Lane::snare, 2.09).judgement == Judgement::pending);    // 실제 시간으로 180ms: 구간 밖
}

void testChartHelpers()
{
    using hvd::Lane;

    CHECK (hvd::subdivisionOf (makeNote (1.0, Lane::kick, 0.8f, -1.0)) == -1);
    CHECK (hvd::subdivisionOf (makeNote (1.0, Lane::kick, 0.8f, 2.0)) == 0);
    CHECK (hvd::subdivisionOf (makeNote (1.0, Lane::kick, 0.8f, 2.25)) == 6);
    CHECK (hvd::subdivisionOf (makeNote (1.0, Lane::kick, 0.8f, 2.5)) == 12);
    CHECK (hvd::subdivisionOf (makeNote (1.0, Lane::kick, 0.8f, 2.0 + 1.0 / 3.0)) == 8);
    CHECK (hvd::subdivisionOf (makeNote (1.0, Lane::kick, 0.8f, 2.99999)) == 0);

    // 셋잇단: 1/3, 2/3 자리가 아홉 개 넘게, 16분음표 엇박의 두 배보다 많아야 한다.
    std::vector<hvd::ChartNote> notes;

    for (int i = 0; i < 9; ++i)
        notes.push_back (makeNote (i, Lane::hihat, 0.8f, i + (i % 2 == 0 ? 1.0 / 3.0 : 2.0 / 3.0)));

    CHECK (hvd::hasTripletFeel (notes));
    notes.pop_back();
    CHECK (! hvd::hasTripletFeel (notes));
    notes.push_back (makeNote (9.0, Lane::hihat, 0.8f, 9.0 + 1.0 / 3.0));

    for (int i = 0; i < 5; ++i)
        notes.push_back (makeNote (10.0 + i, Lane::kick, 0.8f, 10.0 + i + 0.25));

    CHECK (! hvd::hasTripletFeel (notes));    // 9 > 5 × 2가 아니다

    CHECK (hvd::isTomLane (Lane::tomHigh) && hvd::isTomLane (Lane::tomMid) && hvd::isTomLane (Lane::tomFloor));
    CHECK (! hvd::isTomLane (Lane::snare) && ! hvd::isTomLane (Lane::kick));
    CHECK (hvd::isCymbalLane (Lane::crash) && hvd::isCymbalLane (Lane::hihat) && hvd::isCymbalLane (Lane::ride));
    CHECK (! hvd::isCymbalLane (Lane::snare) && ! hvd::isCymbalLane (Lane::tomFloor));

    CHECK (std::string_view (hvd::laneShortName (Lane::crash)) == "CR" && std::string_view (hvd::laneShortName (Lane::ride)) == "RD");
    CHECK (std::string_view (hvd::laneShortName (Lane::tomFloor)) == "T3" && std::string_view (hvd::laneShortName (Lane::kick)) == "KK");
}

void testPatterns()
{
    using hvd::Lane;

    // 연습 패턴: 템포 110, 여덟 마디, 박 35개
    std::vector<double> beats;
    const auto notes = hvd::makePracticePattern (110.0, beats);
    const auto beat = 60.0 / 110.0;

    CHECK (beats.size() == 35 && isNear (static_cast<float> (beats.back()), static_cast<float> (34.0 * beat)));
    CHECK (notes.size() == 96);

    auto sorted = true, onGrid = true;
    std::array<int, hvd::numLanes> perLane {};

    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        sorted = sorted && (i == 0 || notes[i].seconds >= notes[i - 1].seconds);
        onGrid = onGrid && std::abs (notes[i].beat * beat - notes[i].seconds) < 1.0e-9;
        ++perLane[static_cast<std::size_t> (notes[i].lane)];
    }

    CHECK (sorted && onGrid);
    CHECK (perLane[static_cast<std::size_t> (Lane::crash)] == 2);
    CHECK (perLane[static_cast<std::size_t> (Lane::snare)] == 14 && perLane[static_cast<std::size_t> (Lane::kick)] == 18);
    CHECK (perLane[static_cast<std::size_t> (Lane::tomHigh)] == 4 && perLane[static_cast<std::size_t> (Lane::tomFloor)] == 2);
    CHECK (notes.front().lane == Lane::crash && notes.front().seconds == 0.0);

    // 검사용 록 패턴: 16마디, 1초에서 시작
    const auto hits = hvd::makeRockPattern (120.0);
    CHECK (hits.size() == 204);
    CHECK (! hits.empty() && hits.front().seconds == 1.0);

    auto lanesMatch = true;
    auto crashes = 0;
    sorted = true;

    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        hvd::Lane lane = Lane::kick;
        lanesMatch = lanesMatch && hvd::laneForPadId (hits[i].pad, lane) && lane == hits[i].lane;
        sorted = sorted && (i == 0 || hits[i].seconds >= hits[i - 1].seconds);
        crashes += hits[i].lane == Lane::crash ? 1 : 0;
    }

    CHECK (lanesMatch && sorted && crashes == 2);
    CHECK (isNear (static_cast<float> (hits.back().seconds), static_cast<float> (1.0 + (15 * 4.0 + 3.75) * 0.5)));
}

void testMetronomeTiming()
{
    // 템포 120의 16분음표 격자: 6000샘플 (48 kHz)
    constexpr double grid = 6000.0, rate = 48000.0;
    constexpr std::uint64_t beat = 480000;

    CHECK (isNear (static_cast<float> (hvd::gridOffsetMs (beat + 6100, beat, grid, rate)), 100.0f / 48.0f));
    CHECK (isNear (static_cast<float> (hvd::gridOffsetMs (beat + 5900, beat, grid, rate)), -100.0f / 48.0f));
    CHECK (isNear (static_cast<float> (hvd::gridOffsetMs (beat - 100, beat, grid, rate)), -100.0f / 48.0f));
    CHECK (isNear (static_cast<float> (hvd::gridOffsetMs (beat + 3000, beat, grid, rate)), 62.5f));
    CHECK (isNear (static_cast<float> (hvd::gridOffsetMs (beat, beat, grid, rate)), 0.0f));

    hvd::MetronomeTiming timing;
    CHECK (timing.count == 0 && timing.stored() == 0);

    for (const auto ms : { 4.0f, -2.0f, 6.0f })
        timing.add (ms);

    CHECK (timing.count == 3 && timing.stored() == 3 && isNear (timing.lastMs, 6.0f));
    CHECK (isNear (timing.averageAbsMs, 4.0f));
    CHECK (isNear (timing.recent (0), 6.0f) && isNear (timing.recent (2), 4.0f));

    // 고리 버퍼가 차면 오래된 것부터 밀려난다.
    for (int i = 0; i < hvd::MetronomeTiming::historySize; ++i)
        timing.add (static_cast<float> (i));

    CHECK (timing.count == 35 && timing.stored() == hvd::MetronomeTiming::historySize);
    CHECK (isNear (timing.recent (0), 31.0f) && isNear (timing.recent (31), 0.0f));
    CHECK (isNear (timing.averageAbsMs, 15.5f));

    timing.reset();
    CHECK (timing.count == 0 && timing.lastMs == 0.0f && timing.averageAbsMs == 0.0f && timing.offsetsMs[0] == 0.0f);
}

void testChartVariants()
{
    using hvd::Lane;

    // 템포 120, 16마디: 하이햇 16분음표, 킥은 1박과 3박 그리고 16분음표 엇박 하나, 스네어는 2박과 4박에 고스트 하나.
    hvd::Chart chart;
    chart.tempo = 120.0f;
    chart.duration = 34.0;

    for (int i = 0; i <= 66; ++i)
        chart.beats.push_back (1.0 + 0.5 * i);

    for (int beat = 0; beat < 64; ++beat)
    {
        const auto start = 1.0 + 0.5 * beat;

        for (int s = 0; s < 4; ++s)
            chart.notes.push_back (makeNote (start + 0.125 * s, Lane::hihat, 0.6f, beat + 0.25 * s));

        if (beat % 2 == 0)
        {
            chart.notes.push_back (makeNote (start, Lane::kick, 0.9f, beat));
            chart.notes.push_back (makeNote (start + 0.375, Lane::kick, 0.6f, beat + 0.75));
        }
        else
        {
            chart.notes.push_back (makeNote (start, Lane::snare, 0.9f, beat));
            chart.notes.push_back (makeNote (start + 0.25, Lane::snare, 0.2f, beat + 0.5));
        }
    }

    std::stable_sort (chart.notes.begin(), chart.notes.end(), [] (const hvd::ChartNote& a, const hvd::ChartNote& b) { return a.seconds < b.seconds; });

    const auto countLane = [] (const std::vector<hvd::ChartNote>& notes, Lane lane)
    {
        return static_cast<int> (std::count_if (notes.begin(), notes.end(), [lane] (const hvd::ChartNote& note) { return note.lane == lane; }));
    };

    const auto full = hvd::makeChartVariant (chart, hvd::ChartLevel::full);
    const auto hard = hvd::makeChartVariant (chart, hvd::ChartLevel::hard);
    const auto normal = hvd::makeChartVariant (chart, hvd::ChartLevel::normal);
    const auto easy = hvd::makeChartVariant (chart, hvd::ChartLevel::easy);

    CHECK (full.size() == chart.notes.size());
    CHECK (countLane (full, Lane::hihat) == 256);
    CHECK (countLane (hard, Lane::hihat) == 128);       // 8분음표만
    CHECK (countLane (normal, Lane::hihat) == 64);      // 4분음표만
    CHECK (countLane (easy, Lane::hihat) == 0);         // 심벌 없음
    CHECK (countLane (hard, Lane::kick) == 64);         // 16분음표 자리는 남는다
    CHECK (countLane (normal, Lane::kick) == 32);
    CHECK (countLane (easy, Lane::kick) == 32);
    CHECK (countLane (hard, Lane::snare) == 32);        // 고스트 노트는 빠진다
    CHECK (countLane (normal, Lane::snare) == 32);
    CHECK (countLane (easy, Lane::snare) == 32);

    const auto fullStars = hvd::rateChartDifficulty (full);
    const auto hardStars = hvd::rateChartDifficulty (hard);
    const auto normalStars = hvd::rateChartDifficulty (normal);
    const auto easyStars = hvd::rateChartDifficulty (easy);

    CHECK (easyStars > 0.0f && easyStars < normalStars && normalStars < hardStars && hardStars < fullStars && fullStars <= 5.0f);
    CHECK (isNear (fullStars * 10.0f, std::round (fullStars * 10.0f), 1.0e-3f));    // 소수 첫째 자리까지
    CHECK (easyStars < 1.5f && normalStars < 2.5f && fullStars > 2.5f);
    CHECK (hvd::rateChartDifficulty ({}) == 0.0f);

    // 박 격자가 없는 채보도 덜어 낼 수 있다.
    hvd::Chart loose;
    loose.duration = 10.0;

    for (int i = 0; i < 80; ++i)
        loose.notes.push_back (makeNote (0.1 * i, Lane::hihat));

    CHECK (hvd::makeChartVariant (loose, hvd::ChartLevel::easy).size() < hvd::makeChartVariant (loose, hvd::ChartLevel::normal).size());
    CHECK (hvd::makeChartVariant (loose, hvd::ChartLevel::normal).size() < loose.notes.size());
}

/** 받아 적기 검사에 쓰는 합성 드럼 소리. */
struct SynthDrums
{
    static constexpr double sampleRate = 44100.0;

    std::vector<float> audio;
    std::uint32_t noiseState = 12345u;

    explicit SynthDrums (double seconds) : audio (static_cast<std::size_t> (seconds * sampleRate), 0.0f) {}

    float noise() noexcept
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return static_cast<float> (static_cast<std::int32_t> (noiseState)) / 2147483648.0f;
    }

    void add (double seconds, const std::vector<float>& sound, float gain)
    {
        const auto start = static_cast<std::size_t> (seconds * sampleRate);

        for (std::size_t i = 0; i < sound.size() && start + i < audio.size(); ++i)
            audio[start + i] += sound[i] * gain;
    }

    std::vector<float> kick()
    {
        std::vector<float> sound (static_cast<std::size_t> (0.3 * sampleRate));
        auto phase = 0.0;

        for (std::size_t i = 0; i < sound.size(); ++i)
        {
            const auto t = static_cast<double> (i) / sampleRate;
            phase += 6.283185307179586 * (50.0 + 70.0 * std::exp (-t / 0.03)) / sampleRate;
            sound[i] = static_cast<float> (0.9 * std::sin (phase) * std::exp (-t / 0.07)) + (t < 0.002 ? 0.3f * noise() : 0.0f);
        }

        return sound;
    }

    std::vector<float> snare()
    {
        std::vector<float> sound (static_cast<std::size_t> (0.35 * sampleRate));

        for (std::size_t i = 0; i < sound.size(); ++i)
        {
            const auto t = static_cast<double> (i) / sampleRate;
            sound[i] = static_cast<float> (0.5 * std::sin (6.283185307179586 * 190.0 * t) * std::exp (-t / 0.05)
                                           + 0.5 * noise() * std::exp (-t / 0.07));
        }

        return sound;
    }

    std::vector<float> hat()
    {
        std::vector<float> sound (static_cast<std::size_t> (0.12 * sampleRate));
        auto previous = 0.0f, previousDifference = 0.0f;

        for (std::size_t i = 0; i < sound.size(); ++i)
        {
            const auto t = static_cast<double> (i) / sampleRate;
            const auto value = noise();
            const auto difference = value - previous;
            sound[i] = static_cast<float> (0.25 * (difference - previousDifference) * std::exp (-t / 0.02));
            previous = value;
            previousDifference = difference;
        }

        return sound;
    }
};

/** truth의 노트 가운데 notes에서 같은 레인으로 tolerance 안에 찾은 비율과, 찾은 노트들의 평균 시각 오차를 잰다. */
struct MatchResult
{
    double recall = 0.0, precision = 0.0, meanAbsError = 0.0;
};

MatchResult matchLane (const std::vector<hvd::ChartNote>& truth, const std::vector<hvd::ChartNote>& notes, hvd::Lane lane, double tolerance = 0.035)
{
    std::vector<double> wanted, found;

    for (const auto& note : truth)
        if (note.lane == lane)
            wanted.push_back (note.seconds);

    for (const auto& note : notes)
        if (note.lane == lane)
            found.push_back (note.seconds);

    std::vector<bool> used (found.size(), false);
    auto matched = 0;
    auto errorSum = 0.0;

    for (const auto time : wanted)
    {
        auto best = found.size();

        for (std::size_t i = 0; i < found.size(); ++i)
            if (! used[i] && std::abs (found[i] - time) <= tolerance && (best == found.size() || std::abs (found[i] - time) < std::abs (found[best] - time)))
                best = i;

        if (best != found.size())
        {
            used[best] = true;
            ++matched;
            errorSum += std::abs (found[best] - time);
        }
    }

    MatchResult result;
    result.recall = wanted.empty() ? 1.0 : static_cast<double> (matched) / static_cast<double> (wanted.size());
    result.precision = found.empty() ? 1.0 : static_cast<double> (matched) / static_cast<double> (found.size());
    result.meanAbsError = matched > 0 ? errorSum / matched : 0.0;
    return result;
}

void testSpeedTrainer()
{
    // 시작: 지금 속도가 느리면 거기서(5% 단위로), 100%면 70%에서
    hvd::SpeedTrainer trainer;
    trainer.start (1.0f);
    CHECK (isNear (trainer.speed, 0.7f));
    trainer.start (0.82f);
    CHECK (isNear (trainer.speed, 0.8f));

    // 박에 맞음 80% 이상인 바퀴마다 5%씩. 거의 치지 않은 바퀴는 셈하지 않는다
    trainer.start (0.9f);
    CHECK (! trainer.finishLap (3, 100.0f) && trainer.lapsAtSpeed == 0);
    CHECK (! trainer.finishLap (20, 70.0f) && isNear (trainer.speed, 0.9f) && isNear (trainer.lastPercent, 70.0f));
    CHECK (trainer.finishLap (20, 85.0f) && isNear (trainer.speed, 0.95f) && trainer.lapsAtSpeed == 0);
    CHECK (trainer.finishLap (20, 80.0f) && isNear (trainer.speed, 1.0f) && trainer.atTarget());
    CHECK (! trainer.finishLap (20, 100.0f) && isNear (trainer.speed, 1.0f));

    // 박자를 잴 수 없으면 두 바퀴마다
    trainer.start (0.7f);
    CHECK (! trainer.finishLap (20, -1.0f));
    CHECK (trainer.finishLap (20, -1.0f) && isNear (trainer.speed, 0.75f));
}

void testPracticeStats()
{
    // 차이들의 통계: 가운뎃값, 10%째와 90%째 값, 20ms 안에 든 비율
    {
        const auto stats = hvd::timingStatsOf ({ 40.0f, -20.0f, 0.0f, 20.0f, -10.0f, 10.0f });
        CHECK (stats.count == 6);
        CHECK (isNear (stats.medianMs, 5.0f, 1.0e-4f));
        CHECK (isNear (stats.lowMs, -15.0f, 1.0e-4f) && isNear (stats.highMs, 30.0f, 1.0e-4f));
        CHECK (isNear (stats.spreadMs(), 22.5f, 1.0e-4f));
        CHECK (isNear (stats.withinPercent, 100.0f * 5.0f / 6.0f, 1.0e-3f));

        const auto none = hvd::timingStatsOf ({});
        CHECK (none.count == 0 && none.medianMs == 0.0f && none.withinPercent == 0.0f);
    }

    // 템포 120(박 하나 0.5초)의 채보. 노트는 16분음표 자리에 있다.
    hvd::Chart chart;
    chart.tempo = 120.0f;
    chart.firstDownbeat = 0;

    for (int beat = 0; beat < 40; ++beat)
    {
        chart.beats.push_back (0.5 * beat);
        chart.notes.push_back ({ 0.5 * beat, hvd::Lane::kick, 1.0f, static_cast<double> (beat), 0 });
        chart.notes.push_back ({ 0.5 * beat + 0.125, hvd::Lane::hihat, 0.7f, beat + 0.25, 0 });
    }

    CHECK (hvd::practiceGridOf (chart) == 4);

    // 친 것: 박에 정확히 / 16분음표 자리보다 20ms 늦게 / 30ms 빠르게 / 절반 속도로 틀고 곡의 시간으로 40ms 늦게(실제로는 80ms)
    std::vector<hvd::PracticeHit> hits;
    hits.push_back ({ 2.0, hvd::Lane::kick, 1.0f });
    hits.push_back ({ 2.52, hvd::Lane::snare, 1.0f });
    hits.push_back ({ 3.095, hvd::Lane::hihat, 1.0f });
    hits.push_back ({ 4.04, hvd::Lane::kick, 0.5f });
    hits.push_back ({ 4.51, hvd::Lane::tomMid, 1.0f });
    hits.push_back ({ 4.99, hvd::Lane::tomFloor, 1.0f });

    const auto report = hvd::analysePractice (hits, chart, 1);
    CHECK (report.gridPerBeat == 4 && report.offsetsMs.size() == hits.size());

    if (report.offsetsMs.size() == hits.size())
    {
        CHECK (isNear (report.offsetsMs[0], 0.0f, 0.01f) && isNear (report.offsetsMs[1], 20.0f, 0.01f));
        CHECK (isNear (report.offsetsMs[2], -30.0f, 0.01f) && isNear (report.offsetsMs[3], 80.0f, 0.01f));
        CHECK (isNear (report.offsetsMs[4], 10.0f, 0.01f) && isNear (report.offsetsMs[5], -10.0f, 0.01f));
    }

    CHECK (report.all.count == 6);
    CHECK (report.groups[static_cast<std::size_t> (hvd::PracticeGroup::kick)].count == 2);
    CHECK (report.groups[static_cast<std::size_t> (hvd::PracticeGroup::snare)].count == 1);
    CHECK (report.groups[static_cast<std::size_t> (hvd::PracticeGroup::hihat)].count == 1);
    CHECK (report.groups[static_cast<std::size_t> (hvd::PracticeGroup::ride)].count == 0);

    // 탐 셋은 한 무리로 센다
    const auto& toms = report.groups[static_cast<std::size_t> (hvd::PracticeGroup::toms)];
    CHECK (toms.count == 2 && isNear (toms.medianMs, 0.0f, 0.01f));

    // 곡의 흐름: 한 마디(2초)씩. 2~4초에 셋, 4~6초에 셋. 4.99초의 타격은 5.0초 자리에 가장 가깝지만 구간은 친 시각으로 정한다.
    CHECK (report.sections.size() == 2);

    if (report.sections.size() == 2)
    {
        CHECK (isNear (static_cast<float> (report.sections[0].startSeconds), 2.0f) && isNear (static_cast<float> (report.sections[0].endSeconds), 4.0f));
        CHECK (report.sections[0].stats.count == 3 && isNear (report.sections[0].stats.medianMs, 0.0f, 0.01f));
        CHECK (isNear (static_cast<float> (report.sections[1].startSeconds), 4.0f) && report.sections[1].stats.count == 3);
        CHECK (isNear (report.sections[1].stats.medianMs, 10.0f, 0.01f));
    }

    // 셋잇단 느낌의 곡은 박을 셋으로 나눠 본다: 박의 1/3 자리에 친 것은 벗어난 것이 아니다.
    hvd::Chart shuffle;
    shuffle.tempo = 120.0f;

    for (int beat = 0; beat < 40; ++beat)
    {
        shuffle.beats.push_back (0.5 * beat);
        shuffle.notes.push_back ({ 0.5 * beat, hvd::Lane::kick, 1.0f, static_cast<double> (beat), 0 });
        shuffle.notes.push_back ({ 0.5 * beat + 1.0 / 3.0, hvd::Lane::hihat, 0.7f, beat + 2.0 / 3.0, 0 });
    }

    CHECK (hvd::practiceGridOf (shuffle) == 3);
    const auto swung = hvd::analysePractice ({ { 3.0 + 0.5 / 3.0, hvd::Lane::hihat, 1.0f } }, shuffle);
    CHECK (swung.gridPerBeat == 3 && swung.offsetsMs.size() == 1 && isNear (swung.offsetsMs[0], 0.0f, 0.01f));

    // 박을 찾지 못한 채보로는 잴 수 없다
    hvd::Chart noBeats;
    CHECK (hvd::analysePractice (hits, noBeats).offsetsMs.empty());
}

void testTranscriber()
{
    using hvd::Lane;

    // 템포 120, 20마디: 하이햇 8분음표, 킥 1박과 3박(짝수 마디는 2박 반에도), 스네어 2박과 4박.
    SynthDrums synth (42.0);
    const auto kick = synth.kick();
    const auto snare = synth.snare();
    const auto hat = synth.hat();
    std::vector<hvd::ChartNote> truth;

    for (int bar = 0; bar < 20; ++bar)
    {
        const auto barStart = 1.0 + 2.0 * bar;

        for (int eighth = 0; eighth < 8; ++eighth)
        {
            const auto time = barStart + 0.25 * eighth;
            synth.add (time, hat, eighth % 2 == 0 ? 1.0f : 0.7f);
            truth.push_back (makeNote (time, Lane::hihat));

            if (eighth == 0 || eighth == 4 || (eighth == 3 && bar % 2 == 1))
            {
                synth.add (time, kick, 1.0f);
                truth.push_back (makeNote (time, Lane::kick));
            }

            if (eighth == 2 || eighth == 6)
            {
                synth.add (time, snare, 1.0f);
                truth.push_back (makeNote (time, Lane::snare));
            }
        }
    }

    hvd::TranscribeOptions options;
    options.isolatedDrums = true;

    std::atomic<float> progress { 0.0f };
    const auto chart = hvd::transcribeDrums (synth.audio.data(), nullptr, synth.audio.size(), SynthDrums::sampleRate, options, &progress);

    CHECK (progress.load() == 1.0f);
    CHECK (std::abs (chart.duration - 42.0) < 0.01);
    CHECK (std::abs (chart.tempo - 120.0f) < 2.0f);
    CHECK (chart.beats.size() > 70 && chart.beats.size() < 95);

    const auto kicks = matchLane (truth, chart.notes, Lane::kick);
    const auto snares = matchLane (truth, chart.notes, Lane::snare);
    const auto hats = matchLane (truth, chart.notes, Lane::hihat);

    CHECK (kicks.recall >= 0.95 && kicks.precision >= 0.95);
    CHECK (snares.recall >= 0.95 && snares.precision >= 0.95);
    CHECK (hats.recall >= 0.9 && hats.precision >= 0.9);    // 스네어에 가려진 하이햇은 패턴으로 채워야 한다
    CHECK (kicks.meanAbsError < 0.008 && snares.meanAbsError < 0.008 && hats.meanAbsError < 0.008);

    // 박 위에 놓인 노트는 격자 자리가 정수 박이어야 한다.
    auto onGrid = 0, kickNotes = 0;

    for (const auto& note : chart.notes)
    {
        if (note.lane != Lane::kick)
            continue;

        ++kickNotes;
        const auto fraction = note.beat - std::floor (note.beat);
        onGrid += note.beat >= 0.0 && (fraction < 0.01 || std::abs (fraction - 0.5) < 0.01) ? 1 : 0;
    }

    CHECK (kickNotes > 0 && onGrid >= kickNotes * 9 / 10);

    // 취소하면 빈 채보가 나온다.
    std::atomic<bool> cancel { true };
    CHECK (hvd::transcribeDrums (synth.audio.data(), nullptr, synth.audio.size(), SynthDrums::sampleRate, options, nullptr, &cancel).notes.empty());

    // 너무 짧은 소리와 무음에서는 노트가 없다.
    std::vector<float> silence (static_cast<std::size_t> (SynthDrums::sampleRate * 12.0), 0.0f);
    CHECK (hvd::transcribeDrums (silence.data(), nullptr, silence.size(), SynthDrums::sampleRate, options).notes.empty());
    CHECK (hvd::transcribeDrums (silence.data(), nullptr, 1000, SynthDrums::sampleRate, options).notes.empty());
}

void testTranscriberStereo()
{
    using hvd::Lane;

    // 앞 여덟 마디는 왼쪽에 놓인 하이햇, 뒤 여덟 마디는 오른쪽에 놓인 라이드(같은 소리를 자리만 바꿔 쓴다).
    // 소리가 같아도 좌우 위치가 두 무리로 갈리므로 뒤쪽은 라이드 레인으로 가야 한다.
    SynthDrums left (34.0), right (34.0);
    const auto kick = left.kick();
    const auto snare = left.snare();
    const auto hat = left.hat();
    std::vector<hvd::ChartNote> truth;

    for (int bar = 0; bar < 16; ++bar)
    {
        const auto barStart = 1.0 + 2.0 * bar;
        const auto ride = bar >= 8;

        for (int eighth = 0; eighth < 8; ++eighth)
        {
            const auto time = barStart + 0.25 * eighth;
            left.add (time, hat, ride ? 0.25f : 1.0f);
            right.add (time, hat, ride ? 1.0f : 0.35f);
            truth.push_back (makeNote (time, ride ? Lane::ride : Lane::hihat));

            if (eighth == 0 || eighth == 4)
            {
                left.add (time, kick, 1.0f);
                right.add (time, kick, 1.0f);
            }

            if (eighth == 2 || eighth == 6)
            {
                left.add (time, snare, 1.0f);
                right.add (time, snare, 1.0f);
            }
        }
    }

    hvd::TranscribeOptions options;
    options.isolatedDrums = true;
    const auto chart = hvd::transcribeDrums (left.audio.data(), right.audio.data(), left.audio.size(), SynthDrums::sampleRate, options);

    const auto hats = matchLane (truth, chart.notes, Lane::hihat);
    const auto rides = matchLane (truth, chart.notes, Lane::ride);

    CHECK (std::abs (chart.tempo - 120.0f) < 2.0f);
    CHECK (hats.recall >= 0.85 && hats.precision >= 0.85);
    CHECK (rides.recall >= 0.85 && rides.precision >= 0.85);

    // 모노로 주면 가를 근거가 없으므로 모두 하이햇으로 둔다.
    std::vector<float> mono (left.audio.size());

    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5f * (left.audio[i] + right.audio[i]);

    const auto monoChart = hvd::transcribeDrums (mono.data(), nullptr, mono.size(), SynthDrums::sampleRate, options);
    CHECK (std::count_if (monoChart.notes.begin(), monoChart.notes.end(), [] (const hvd::ChartNote& note) { return note.lane == Lane::ride; }) == 0);
}

void testTranscriberMix()
{
    using hvd::Lane;

    // 같은 패턴에 길게 이어지는 베이스와 화음을 섞고, 완성곡으로 받아 적는다. 킥과 스네어는 대부분 찾아야 한다.
    SynthDrums synth (34.0);
    const auto kick = synth.kick();
    const auto snare = synth.snare();
    const auto hat = synth.hat();
    std::vector<hvd::ChartNote> truth;

    for (int bar = 0; bar < 16; ++bar)
    {
        const auto barStart = 1.0 + 2.0 * bar;

        for (int eighth = 0; eighth < 8; ++eighth)
        {
            const auto time = barStart + 0.25 * eighth;
            synth.add (time, hat, 0.8f);

            if (eighth == 0 || eighth == 4)
            {
                synth.add (time, kick, 1.0f);
                truth.push_back (makeNote (time, Lane::kick));
            }

            if (eighth == 2 || eighth == 6)
            {
                synth.add (time, snare, 1.0f);
                truth.push_back (makeNote (time, Lane::snare));
            }
        }
    }

    // 베이스(마디마다 음이 바뀌는 톱니파)와 화음(사인파 셋), 2초마다 음이 바뀐다.
    const double bassNotes[] = { 55.0, 65.4, 49.0, 73.4 };

    for (std::size_t i = 0; i < synth.audio.size(); ++i)
    {
        const auto t = static_cast<double> (i) / SynthDrums::sampleRate;
        const auto bar = static_cast<int> (std::max (0.0, t - 1.0) / 2.0) % 4;
        const auto bass = bassNotes[bar];
        const auto phase = std::fmod (t * bass, 1.0);
        auto value = 0.25 * (2.0 * phase - 1.0);

        for (const auto ratio : { 4.0, 5.0, 6.0 })
            value += 0.08 * std::sin (6.283185307179586 * bass * ratio * t);

        synth.audio[i] += static_cast<float> (value);
    }

    hvd::TranscribeOptions options;
    options.isolatedDrums = false;
    const auto chart = hvd::transcribeDrums (synth.audio.data(), nullptr, synth.audio.size(), SynthDrums::sampleRate, options);

    const auto kicks = matchLane (truth, chart.notes, Lane::kick, 0.045);
    const auto snares = matchLane (truth, chart.notes, Lane::snare, 0.045);

    CHECK (std::abs (chart.tempo - 120.0f) < 2.0f);
    CHECK (kicks.recall >= 0.85 && kicks.precision >= 0.8);
    CHECK (snares.recall >= 0.85 && snares.precision >= 0.8);
    CHECK (kicks.meanAbsError < 0.02 && snares.meanAbsError < 0.02);
}

} // namespace

int hvd::runCoreTests (std::string& log)
{
    numChecks = 0;
    numFailures = 0;
    testLog = &log;

    // 테스트를 함수 포인터 표로 하나씩 부른다. volatile로 두어 컴파일러가 전부 한 함수 안에 펼치지 못하게 한다.
    // (펼치면 테스트마다 만드는 엔진들이 한 스택 프레임에 몰려 스택이 넘칠 수 있다.)
    using TestFunction = void (*)();
    static TestFunction volatile allTests[] = {
        testQueue,
        testKeyCodes,
        testReportInterval,
        testMapping,
        testHitAndVelocity,
        testOverlap,
        testDelay,
        testChokeGroup,
        testChokePad,
        testSympathetic,
        testSkipAndChokeCount,
        testRoundRobin,
        testLayers,
        testResampledPlayback,
        testPan,
        testVoiceStealing,
        testStealOrder,
        testRollKeepsCymbals,
        testKitSwap,
        testQueueOverflow,
        testParseBeats,
        testStrokeResolve,
        testSingleAndDownUp,
        testDoubleAndFlam,
        testRepeat,
        testRepeatSwell,
        testRepeatFollowsTempo,
        testRepeatFollow,
        testBuzz,
        testPattern,
        testModifiers,
        testPedal,
        testPedalReleasedSwap,
        testFootSplash,
        testMouseHit,
        testMouseTargets,
        testChordAccent,
        testTapTempo,
        testMetronome,
        testHumanize,
        testMappingChangeStopsRunners,
        testRunnerLimit,
        testPadParams,
        testFieldTables,
        testHitEventsAndPeaks,
        testHitAtAndAux,
        testHitOrigin,
        testEffects,
        testPeakLimiter,
        testTrackPlayback,
        testTrackSpeed,
        testTrackLoopAndSeek,
        testTrackResample,
        testReplay,
        testTrackLeadIn,
        testLanes,
        testPadKinds,
        testStandardPads,
        testRhythmGame,
        testChartHelpers,
        testPatterns,
        testChartVariants,
        testMetronomeTiming,
        testPracticeStats,
        testSpeedTrainer,
        testTranscriber,
        testTranscriberStereo,
        testTranscriberMix,
    };

    for (auto& entry : allTests)
    {
        const TestFunction test = entry;
        test();
    }

    log += std::to_string (numChecks) + " checks, " + std::to_string (numFailures) + " failures\n";
    testLog = nullptr;
    return numFailures;
}
