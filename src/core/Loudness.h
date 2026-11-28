#pragma once

#include <cstddef>

#include "Kit.h"

namespace hvd
{

/** BS.1770의 K 가중 순간 크기(400ms 창)의 최댓값(LUFS). 소리의 앞 maxSeconds만 본다.
    right가 nullptr이면 모노 소리를 양쪽으로 낸 것으로 본다 (엔진이 모노 샘플을 그렇게 낸다). 소리가 없으면 -100. */
float momentaryMaxLoudness (const float* left, const float* right, std::size_t frames, double sampleRate, double maxSeconds = 2.0) noexcept;

/** 샘플 하나의 크기 (momentaryMaxLoudness). 디코드할 때 한 번 재 둔다 (Sample::loudness). */
float sampleLoudness (const Sample& sample) noexcept;

/** 세기 레이어의 경계에서 소리 크기가 튀지 않게 곱할 보정(dB). layer는 velocity로 고른 레이어의 번호, curve는 패드의 세기 곡선.

    레이어를 바꾸는 세기(upTo)에서 녹음된 크기 차이만큼 소리가 뛴다 (예: 0.67에서 5~9dB).
    레이어마다 가운데 세기(아래·위 경계의 기하 평균, 맨 아래 레이어는 위 경계의 절반)의 크기는 그대로 두고,
    이웃 레이어의 가운데 사이는 세기의 로그에 대해 곧게 잇는다. 맨 아래 가운데보다 약하거나 맨 위 가운데보다 센 타격은 그대로다.
    크기를 재지 않은 레이어가 끼거나, 위 레이어가 오히려 작으면 손대지 않는다. ±12dB를 넘지 않는다. */
float layerStepCorrectionDb (const Pad& pad, std::size_t layer, float velocity, float curve) noexcept;

} // namespace hvd
