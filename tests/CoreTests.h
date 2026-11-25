#pragma once

#include <string>

namespace hvd
{

/** 코어 자동 테스트를 모두 돌린다. 결과를 log에 적고 실패한 검사 수를 돌려준다. */
int runCoreTests (std::string& log);

} // namespace hvd
