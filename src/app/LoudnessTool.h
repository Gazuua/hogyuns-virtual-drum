#pragma once

#include <juce_core/juce_core.h>

/** --loudness: 킷의 패드마다 실제로 치는 세기로 한 번 쳐서 크기를 재고, 패드 음량을 맞춘다.

    엔진으로 소리를 내서(이펙트 없이, 패드 음량·세기 곡선·함께 울리는 패드 포함) BS.1770 K 가중 순간 크기(400ms)의 최댓값을 잰다.
    치는 세기는 키 배열에서 그 패드를 한 번 치는 키의 세기(여럿이면 가장 센 것), 키가 없으면 마우스(0.9)다.

    - 그냥 돌리면 보고만 한다: 패드마다 크기, 스네어와의 차이, 목표와의 차이, 맞출 때의 음량.
    - match: 다른 킷(예: 음량맞춤)의 같은 패드와 같은 크기가 되게.
    - targets: 스네어 기준 목표(킥 +0.7dB, 하이햇 닫힘 -6dB 등, LoudnessTool.cpp의 표)가 되게.
    - pads를 주면 그 패드만 바꾼다. apply를 주어야 kit.json에 적는다 (바꾸기 전 내용은 backups/에 남긴다).
    - layers를 주면 세기 레이어의 경계에서 크기가 얼마나 뛰는지(잇기 전과 후)도 적는다.
    - smoothLayers를 끄면 경계를 잇지 않은 엔진으로 잰다. */
struct LoudnessOptions
{
    juce::File kitFile;
    juce::File keymapFile;
    juce::File referenceKitFile;    // match
    bool useTargets = false;
    juce::StringArray pads;
    bool apply = false;
    bool showLayers = false;
    bool smoothLayers = true;       // false면 세기 레이어의 경계를 잇지 않고 잰다 (견주어 볼 때)
    juce::File backupsRoot, dataRoot;
};

/** 보고서를 돌려준다. 킷을 읽지 못했거나 적지 못했으면 ok가 false다. */
juce::String runLoudnessTool (const LoudnessOptions& options, bool& ok);
