#pragma once

#include <vector>

#include "Chart.h"

namespace hvd
{

/** 리듬 게임의 연습 패턴: 여덟 마디 8비트 록. 하이햇(뒤 네 마디는 라이드) 8분음표, 킥 1박과 3박,
    스네어 2박과 4박, 넷째와 여덟째 마디 끝에 탐 필인. 0박에서 시작하고 beats에 박 35개(0 ~ 34박)의 시각을 넣는다. */
std::vector<ChartNote> makePracticePattern (double tempo, std::vector<double>& beats);

/** 검사용 패턴의 타격 하나. */
struct PatternHit
{
    double seconds;
    const char* pad;
    float velocity;
    Lane lane;
};

/** 받아 적기 검사(--charttest)에 쓰는 16마디 록 패턴. 앞 여덟 마디는 하이햇, 뒤 여덟 마디는 라이드이고
    필인이 두 번 들어 있다. 1초에서 시작하고 시각 순이다. */
std::vector<PatternHit> makeRockPattern (double tempo);

} // namespace hvd
