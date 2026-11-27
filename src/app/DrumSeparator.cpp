#include "DrumSeparator.h"

#include "Text.h"

namespace
{

constexpr int startSeconds = 240;    // 도구가 준비될 때까지 기다리는 시간 (처음에는 모델을 내려받는다)
constexpr int jobSeconds = 1200;

} // namespace

DrumSeparator::DrumSeparator (const juce::File& dataRoot)
    : python (dataRoot.getChildFile ("tools").getChildFile ("demucs-env").getChildFile ("Scripts").getChildFile ("python.exe")),
      script (dataRoot.getChildFile ("tools").getChildFile ("separate_drums.py"))
{
}

DrumSeparator::~DrumSeparator()
{
    stop();
}

bool DrumSeparator::isInstalled() const
{
    return python.existsAsFile() && script.existsAsFile();
}

juce::String DrumSeparator::getDevice() const
{
    const juce::ScopedLock scope (lock);
    return device;
}

juce::String DrumSeparator::start (Caller& caller)
{
    // 띄우는 동안 다른 스레드는 여기서 기다린다.
    const juce::ScopedLock scope (lock);

    if (queue != juce::File() && process.isRunning())
        return {};

    stopUnlocked();

    queue = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getChildFile ("hvd-separator-" + juce::String::toHexString (juce::Time::currentTimeMillis()));
    queue.createDirectory();

    const juce::StringArray command { python.getFullPathName(), script.getFullPathName(), "--queue", queue.getFullPathName() };

    if (! process.start (command, 0))
        return ko ("드럼 분리 도구를 실행하지 못했습니다");

    // 모델을 읽을 때까지 기다린다. 처음 쓸 때는 모델을 내려받느라 오래 걸릴 수 있다.
    const auto ready = queue.getChildFile ("ready");

    for (int i = 0; i < startSeconds * 10; ++i)
    {
        if (ready.existsAsFile())
        {
            device = ready.loadFileAsString().trim();    // "cuda" 또는 "cpu"
            return {};
        }

        if (! process.isRunning() || caller.shouldStop())
            break;

        caller.pause (100);
    }

    stopUnlocked();
    return ko ("드럼 분리 도구가 준비되지 않았습니다");
}

void DrumSeparator::stop()
{
    const juce::ScopedLock scope (lock);
    stopUnlocked();
}

void DrumSeparator::stopUnlocked()
{
    if (queue != juce::File())
    {
        queue.getChildFile ("quit").replaceWithText ("quit");

        if (process.isRunning() && ! process.waitForProcessToFinish (1500))
            process.kill();

        queue.deleteRecursively();
        queue = juce::File();
    }
}

juce::String DrumSeparator::separate (const juce::File& input, const juce::File& drums, const juce::File& backing, Caller& caller)
{
    auto* request = new juce::DynamicObject();
    request->setProperty ("input", input.getFullPathName());
    request->setProperty ("drums", drums.getFullPathName());
    request->setProperty ("backing", backing.getFullPathName());
    const auto requestText = juce::JSON::toString (juce::var (request));

    juce::File folder;

    {
        const juce::ScopedLock scope (lock);
        folder = queue;
    }

    if (folder == juce::File())
        return ko ("드럼 분리 도구가 중간에 끝났습니다");

    // 다 쓴 뒤에 이름을 바꿔서, 도구가 쓰다 만 파일을 읽지 않게 한다. 도구는 이름 순서대로 하나씩 처리한다.
    const auto name = "job" + juce::String (jobCount.fetch_add (1) + 1).paddedLeft ('0', 6);
    const auto pending = folder.getChildFile (name + ".tmp");
    pending.replaceWithText (requestText);
    pending.moveFileTo (folder.getChildFile (name + ".job"));

    caller.setStage (ko ("드럼 분리 중"));
    const auto done = folder.getChildFile (name + ".done");
    const auto progressFile = folder.getChildFile (name + ".progress");

    for (int i = 0; i < jobSeconds * 10; ++i)
    {
        if (done.existsAsFile())
        {
            const auto text = done.loadFileAsString().trim();
            return text == "ok" ? juce::String() : ko ("드럼 분리 실패: ") + text;
        }

        if (caller.shouldStop())
            return "cancelled";

        if (i % 10 == 0)
        {
            const juce::ScopedLock scope (lock);

            if (queue != folder || ! process.isRunning())
                return ko ("드럼 분리 도구가 중간에 끝났습니다");
        }

        if (i % 5 == 0 && progressFile.existsAsFile())
            caller.setFraction (progressFile.loadFileAsString().getFloatValue());

        caller.pause (100);
    }

    return ko ("드럼 분리가 너무 오래 걸립니다");
}
