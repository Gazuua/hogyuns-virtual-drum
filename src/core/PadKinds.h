#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Kit.h"

namespace hvd
{

/** 패드 id로 짐작한 악기. 화면의 색, 리듬 게임의 레인, 심벌 잡기가 모두 이 규칙을 쓴다. */
enum class Instrument : std::uint8_t { kick, snare, hihat, tom, ride, crash, china, other };

/** 패드 id의 앞머리로 악기를 짐작한다. 대소문자는 가리지 않는다.
    kick·bass·bd 킥, snare·sidestick·rim·clap·sd 스네어, hihat·hat·hh 하이햇, tom 탐,
    ride 라이드, crash·splash·cymbal 크래시, china 차이나. */
Instrument instrumentOfPad (std::string_view padId) noexcept;

/** 하이햇, 라이드, 크래시, 차이나. */
bool isCymbal (Instrument instrument) noexcept;

/** 패드가 속한 악기의 이름: 소문자로 바꾸고 끝말(_bell, _edge, _rim, _closed …)을 뗀 id. 사이드스틱은 snare.
    같은 악기의 패드는 오른쪽 단추로 잡을 때 함께 멈춘다 (라이드와 라이드 벨, 스네어와 림샷). */
std::string padFamily (std::string_view padId);

/** 하이햇이 열린 정도: 닫힘 0, 반열림(half) 0.45, 열림(open) 1. 하이햇이 아니면 0. */
float hihatOpenness (std::string_view padId) noexcept;

/** 패드마다 함께 멈출 패드 번호들: 같은 악기(padFamily)의 패드와 같은 초크 그룹의 패드. 자기 자신도 든다. */
std::vector<std::vector<std::uint16_t>> chokeSetsOf (const Kit& kit);

/** text가 prefix로 시작하는지. 대소문자는 가리지 않는다 (prefix는 소문자로 준다). */
bool startsWithIgnoringCase (std::string_view text, std::string_view prefix) noexcept;

/** text에 part가 들어 있는지. 대소문자는 가리지 않는다 (part는 소문자로 준다). */
bool containsIgnoringCase (std::string_view text, std::string_view part) noexcept;

} // namespace hvd
