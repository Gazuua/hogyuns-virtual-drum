#!/usr/bin/env python3
"""모아 둔 소리를 만든다.

external/sample-libraries 에 받아 둔 드럼 라이브러리(CC0, CC BY, CC BY-SA. 출처는 kits/default/CREDITS.txt)에서 악기별 소리를 골라, 마이크별로 나뉜
파일을 하나로 섞고 48kHz FLAC으로 저장한 뒤, 킷의 패드마다 "sounds" 목록에 적어 넣는다.
프로그램의 킷 화면에서 패드마다 이 목록 가운데 하나를 골라 쓴다.

    pwsh scripts/fetch-samples.ps1                        # 라이브러리 받기 (처음 한 번)
    python -I tools/build_sounds.py                       # 아직 없는 소리를 모두 만든다
    python -I tools/build_sounds.py --list                # 무엇을 만들 수 있는지만 본다
    python -I tools/build_sounds.py --only snare,kick     # 이 패드의 소리만
    python -I tools/build_sounds.py --force               # 이미 있는 소리도 다시 만든다

소리 하나는 "파일 이름 틀" 하나로 적는다. 틀의 {mic}은 마이크, {v}는 세기 단계, {r}은 번갈아 재생 번호다.
라이브러리마다 폴더 구성은 달라도 파일 이름에 세기 단계와 번갈아 재생 번호가 들어 있어서 이것으로 충분하다.

음량은 패드에 원래 들어 있던 소리(기본 킷의 것)에 맞춘다: 가장 센 단계의 타격들의 최댓값을 재고, 그 가운뎃값이
같아지게 한다. (타격 하나만 유난히 큰 소리가 있어서 최댓값끼리 맞추면 나머지가 작아진다.)
ffmpeg와 ffprobe가 PATH에 있어야 한다.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIBRARIES = ROOT / "external" / "sample-libraries"
SAMPLE_RATE = 48000
FADE_OUT_SECONDS = 1.5

# 패드마다: 세기 단계 수, 번갈아 재생 수, 최대 길이(초). 기본 킷과 같은 값이다.
SHAPES = {
    "kick": (6, 3, None), "snare": (6, 4, None), "snare_rim": (4, 3, None), "sidestick": (3, 3, None),
    "hihat_closed": (5, 4, None), "hihat_half": (3, 3, 2.5), "hihat_open": (4, 3, 4.5), "hihat_pedal": (3, 3, None),
    "tom_high": (4, 3, None), "tom_mid": (4, 3, None), "tom_floor": (4, 3, None), "tom_low": (4, 3, None),
    "crash1": (4, 2, 7.0), "crash2": (4, 2, 7.0), "china": (3, 2, 5.0),
    "ride": (5, 2, 6.0), "ride_bell": (4, 2, 6.0), "ride_edge": (3, 2, 7.0),
}

# 패드에 원래 들어 있던 소리에 붙일 이름과 계열. (패드 하나짜리 악기는 계열이 없다)
ORIGINALS = {
    "kick": ("Big Rusty 24인치", ""),
    "snare": ("Big Rusty 14인치", "big-rusty/snare"),
    "snare_rim": ("Big Rusty 14인치", "big-rusty/snare"),
    "sidestick": ("Big Rusty 14인치", "big-rusty/snare"),
    "hihat_closed": ("Big Rusty 14인치", "big-rusty/hihat"),
    "hihat_half": ("Big Rusty 14인치", "big-rusty/hihat"),
    "hihat_open": ("Big Rusty 14인치", "big-rusty/hihat"),
    "hihat_pedal": ("Big Rusty 14인치", "big-rusty/hihat"),
    "tom_high": ("Big Rusty 14인치", "big-rusty/toms"),
    "tom_mid": ("Big Rusty 15인치", "big-rusty/toms"),
    "tom_floor": ("Big Rusty 18인치", "big-rusty/toms"),
    "tom_low": ("Big Rusty 22인치", "big-rusty/toms"),
    "crash1": ("Big Rusty 17인치", ""),
    "crash2": ("Big Rusty 17인치 시즐", ""),
    "china": ("Big Rusty 18인치 차이나", ""),
    "ride": ("Big Rusty 22인치", "big-rusty/ride"),
    "ride_bell": ("Big Rusty 22인치", "big-rusty/ride"),
    "ride_edge": ("Big Rusty 22인치", "big-rusty/ride"),
}

# 마이크 배율은 각 라이브러리의 기본 프로그램이 정해 둔 값(0~127)을 127로 나눈 것이다.
BR_CYMBAL = {"cl": 0.31, "oh": 0.79}
FRANK = {"top": 0.47, "btm": 0.79, "oh": 0.79}
# 여기서는 타격이 또렷하도록 조금 섞는다.
ONE_FILE = {"-": 1.0}    # 마이크가 나뉘지 않은 소리
# Gogodze Phu Vol II의 기본 프로그램(Kit.sfz, 마이크 섞기 80): 킥은 킥 마이크 100%에 나머지 60%, 다른 악기는 모두 100%.
# 오버헤드는 오른쪽, 옆 마이크는 왼쪽. 창가(wndw)와 레트로 마이크는 기본에서 꺼져 있다.
GOGODZE_KICK = {"kick_mic": 1.0, "snare_mic": 0.6, "oh_mic": (0.6, "R"), "side_mic": (0.6, "L"), "front_mic": 0.6}
GOGODZE = {"kick_mic": 1.0, "snare_mic": 1.0, "oh_mic": (1.0, "R"), "side_mic": (1.0, "L"), "front_mic": 1.0}
VIRT = {"kickmic": 1.0, "snaremic": 1.0, "oh": 1.0}

SOUNDS: list[dict] = []


# 패드 하나로 끝나는 악기. 계열로 묶어 함께 바꾸지 않는다 (크래시 둘이 같은 소리가 되면 안 된다).
SINGLE_PADS = {"kick", "crash1", "crash2", "china"}


def sound(pad: str, sound_id: str, name: str, lib: str, family: str, files: str, mics: dict, **extra) -> None:
    if pad in SINGLE_PADS:
        family = ""

    SOUNDS.append(dict(pad=pad, id=sound_id, name=name, lib=lib, family=family, files=files, mics=mics, **extra))


# ---------------------------------------------------------------------------------------------------
# Big Rusty Drums: 기본 킷에 넣지 않은 것
sound("kick", "big-rusty-nodamp", "Big Rusty 24인치 (댐핑 없음, 길게 울림)", "big-rusty-drums", "big-rusty/kick-nodamp",
      "Samples/kick_24_nodamp/kick/{mic}/k_nodamp_vl{v}_rr{r}.flac", {"kick": 0.79, "oh": 0.30})

for pad, part, label in (("ride", "rd/{mic}/rds", ""), ("ride_bell", "bl/{mic}/rds_bl", ""), ("ride_edge", "ed/{mic}/rds_ed", "")):
    sound(pad, "big-rusty-sizzle-19", "Big Rusty 19인치 시즐 라이드", "big-rusty-drums", "big-rusty/ride-sizzle-19",
          f"Samples/ride_sizzle_19/{part}_vl{{v}}_rr{{r}}.flac", BR_CYMBAL)

# 크래시 둘은 서로의 원래 소리도 고를 수 있게 한다.
sound("crash2", "big-rusty-17", "Big Rusty 17인치", "big-rusty-drums", "",
      "Samples/crash_17/cr/{mic}/cr_vl{v}_rr{r}.flac", BR_CYMBAL)
sound("crash1", "big-rusty-sizzle-17", "Big Rusty 17인치 시즐", "big-rusty-drums", "",
      "Samples/crash_sizzle_17/cr/{mic}/crs_vl{v}_rr{r}.flac", BR_CYMBAL)

for pad in ("china", "crash1", "crash2"):
    sound(pad, "big-rusty-stack-edge", "Big Rusty 스택 심벌 (가장자리)", "big-rusty-drums", "big-rusty/stack",
          "Samples/stack_3_layer/ed/{mic}/st_ed_vl{v}_rr{r}.flac", BR_CYMBAL)
    sound(pad, "big-rusty-stack-mid", "Big Rusty 스택 심벌 (가운데)", "big-rusty-drums", "big-rusty/stack-mid",
          "Samples/stack_3_layer/mid/{mic}/st_mid_vl{v}_rr{r}.flac", BR_CYMBAL)

for pad in ("crash1", "crash2"):
    sound(pad, "big-rusty-china", "Big Rusty 18인치 차이나", "big-rusty-drums", "big-rusty/china",
          "Samples/china_18/cn/{mic}/cn_vl{v}_rr{r}.flac", BR_CYMBAL)

# ---------------------------------------------------------------------------------------------------
# Frankensnare: 스네어 여러 대. (폴더 이름, 화면 이름, 림샷의 파일 틀)
FRANK_SNARES = [
    ("10", "10x6 애시", None),
    ("12", "12x5 스틸", None),
    ("13b", "13x9 자작나무", "Samples/13b_rimshot/{mic}/sn_vl{v}_rr{r}.flac"),
    ("14d", "14x5 메이플", None),
    ("14p", "14x6.5 메이플", None),
    ("14a", "14x8 알루미늄", "Samples/14a_rimshot/{mic}/sr_vl{v}_rr{r}.flac"),
    ("14s", "14x8 자작나무", None),
    ("14t", "14x5 (출처 모름)", None),
    ("16", "16x7 사펠리", None),
    ("20", "20x6 포플러", None),
    ("20m", "20x12 오리나무", None),
    ("22", "22x5 마호가니", None),
]

for code, label, rimshot in FRANK_SNARES:
    name = f"Frankensnare {label}"
    family = f"frankensnare/{code}"
    sound("snare", f"frank-{code}", name, "frankensnare", family, f"Samples/{code}_basic/{{mic}}/sn_vl{{v}}_rr{{r}}.flac", FRANK)
    sound("sidestick", f"frank-{code}", name, "frankensnare", family, f"Samples/{code}_basic/{{mic}}/ss_vl{{v}}_rr{{r}}.flac", FRANK)

    if rimshot:
        sound("snare_rim", f"frank-{code}", name, "frankensnare", family, rimshot, FRANK)

sound("snare", "frank-14p-muted", "Frankensnare 14x6.5 메이플 (뮤트)", "frankensnare", "frankensnare/14p-muted",
      "Samples/14p_muted/{mic}/sm_vl{v}_rr{r}.flac", FRANK)
sound("snare", "frank-20m-tight", "Frankensnare 20x12 오리나무 (조인 것)", "frankensnare", "frankensnare/20m-tight",
      "Samples/20m_tight/{mic}/sn_vl{v}_rr{r}.flac", FRANK)

# ---------------------------------------------------------------------------------------------------
# Virtuosity Drums: 재즈 킷. 마이크 이름이 폴더와 파일 이름 양쪽에 들어 있다.
def virtuosity(part: str, round_robin: bool) -> str:
    return f"Samples/{{mic}}/{part.split('_')[0]}/{{mic}}_{part}_vl{{v}}" + ("_rr{r}.flac" if round_robin else ".flac")


sound("kick", "virtuosity", "Virtuosity 킥", "virtuosity-drums", "virtuosity/kick", virtuosity("kick_snoff", True), VIRT)

for pad, part in (("snare", "snare_center"), ("snare_rim", "snare_rimshot"), ("sidestick", "snare_crossstick")):
    sound(pad, "virtuosity", "Virtuosity 스네어", "virtuosity-drums", "virtuosity/snare", virtuosity(part, False), VIRT)

for pad, part in (("snare", "snareoff_center"), ("sidestick", "snareoff_crossstick")):
    sound(pad, "virtuosity-off", "Virtuosity 스네어 (와이어를 푼 것)", "virtuosity-drums", "virtuosity/snare-off",
          virtuosity(part, False), VIRT)

for pad, part, rr in (("hihat_closed", "hh_closed", True), ("hihat_half", "hh_half", True),
                      ("hihat_open", "hh_open", True), ("hihat_pedal", "hh_pedal", True)):
    sound(pad, "virtuosity", "Virtuosity 하이햇", "virtuosity-drums", "virtuosity/hihat", virtuosity(part, rr), VIRT)

# 탐이 둘뿐이라, 가운데 탐과 가장 낮은 탐은 음높이를 내려서 만든다.
for pad, part, pitch, label in (("tom_high", "htom_center", 0, "높은 탐"), ("tom_mid", "htom_center", -3, "높은 탐을 3반음 내린 것"),
                                ("tom_floor", "ltom_center", 0, "낮은 탐"), ("tom_low", "ltom_center", -4, "낮은 탐을 4반음 내린 것")):
    sound(pad, "virtuosity-toms", f"Virtuosity {label}", "virtuosity-drums", "virtuosity/toms", virtuosity(part, False), VIRT, pitch=pitch)

for pad in ("crash1", "crash2"):
    sound(pad, "virtuosity-crash", "Virtuosity 크래시", "virtuosity-drums", "virtuosity/crash", virtuosity("crash_crash", True), VIRT)
    sound(pad, "virtuosity-sizzle", "Virtuosity 크래시 (시즐)", "virtuosity-drums", "virtuosity/crash-sizzle",
          virtuosity("crash_sizzle", True), VIRT)

for pad, part in (("ride", "ride_ride"), ("ride_bell", "ride_bell")):
    sound(pad, "virtuosity", "Virtuosity 라이드", "virtuosity-drums", "virtuosity/ride", virtuosity(part, True), VIRT)

sound("ride", "virtuosity-flat", "Virtuosity 플랫 라이드", "virtuosity-drums", "virtuosity/flatride", virtuosity("flatride_ride", True), VIRT)


# ---------------------------------------------------------------------------------------------------
# Swirly Drums: 브러시 킷. 킥과 스네어만 마이크가 둘이고(파일 이름 끝에 붙는다) 나머지는 파일 하나다.
sound("kick", "swirly-marching", "Swirly 마칭 베이스 드럼", "swirly-drums", "",
      "Samples/marching_kick/marching_kick_vl{v}_rr{r}_{mic}.wav", {"beater": 0.79, "reso": 0.79})
sound("kick", "swirly-cajon", "Swirly 카혼", "swirly-drums", "",
      "Samples/cajon_kick/cajon_kick_vl{v}_rr{r}_{mic}.wav", {"front": 0.79, "rear": 0.79})
sound("snare", "swirly", "Swirly 스네어 (브러시)", "swirly-drums", "swirly/snare",
      "Samples/snare_main/snare_hit_vl{v}_rr{r}_{mic}.wav", {"top": 0.79, "btm": 0.50})

for pad, folder, part in (("hihat_closed", "hat_closed", "hh_closed"), ("hihat_half", "hat_half", "hh_half"),
                          ("hihat_open", "hat_open", "hh_open"), ("hihat_pedal", "hat_foot", "hh_foot")):
    sound(pad, "swirly", "Swirly 하이햇", "swirly-drums", "swirly/hihat", f"Samples/{folder}/{part}_vl{{v}}_rr{{r}}.wav", ONE_FILE)

for pad, part in (("tom_high", "tom_mhi"), ("tom_mid", "tom_xmed"), ("tom_floor", "tom_mlow"), ("tom_low", "tom_floor")):
    sound(pad, "swirly-toms", "Swirly 탐", "swirly-drums", "swirly/toms", f"Samples/{part}/{part}_vl{{v}}_rr{{r}}.wav", ONE_FILE)

for pad in ("crash1", "crash2"):
    sound(pad, "swirly-crash", "Swirly 크래시", "swirly-drums", "", "Samples/crash/crash_vl{v}_rr{r}.wav", ONE_FILE)
    sound(pad, "swirly-crash-broken", "Swirly 깨진 크래시", "swirly-drums", "", "Samples/crash_broken/crash_vl{v}_rr{r}.wav", ONE_FILE)
    sound(pad, "swirly-splash", "Swirly 스플래시", "swirly-drums", "", "Samples/splash/splash_vl{v}_rr{r}.wav", ONE_FILE)

sound("china", "swirly-china", "Swirly 차이나", "swirly-drums", "", "Samples/china/china_vl{v}_rr{r}.wav", ONE_FILE)
sound("ride", "swirly", "Swirly 라이드", "swirly-drums", "swirly/ride", "Samples/ride/ride_vl{v}_rr{r}.wav", ONE_FILE)

# ---------------------------------------------------------------------------------------------------
# Gogodze Phu Vol II: 1973년 김정미 "Now" 앨범의 드럼 소리를 노리고 만든 작은 킷(킥, 스네어, 탐 셋, 하이햇).
# 파일 이름: Samples/<마이크>/<주법>_vl<세기>_rr<번갈아>.wav. 탐이 셋이라 가장 낮은 탐은 낮은 탐을 3반음 내려서 만든다.
sound("kick", "gogodze", "Gogodze 킥 (70년대)", "gogodze-phu-vol-ii", "", "Samples/{mic}/ks_vl{v}_rr{r}.wav", GOGODZE_KICK)

for pad, part in (("snare", "sc"), ("snare_rim", "se"), ("sidestick", "ss")):
    sound(pad, "gogodze", "Gogodze 스네어 (70년대)", "gogodze-phu-vol-ii", "gogodze/snare", f"Samples/{{mic}}/{part}_vl{{v}}_rr{{r}}.wav", GOGODZE)

for pad, part in (("hihat_closed", "ht"), ("hihat_half", "hl"), ("hihat_open", "ho"), ("hihat_pedal", "hf")):
    sound(pad, "gogodze", "Gogodze 하이햇 (70년대)", "gogodze-phu-vol-ii", "gogodze/hihat", f"Samples/{{mic}}/{part}_vl{{v}}_rr{{r}}.wav", GOGODZE)

for pad, part, pitch, label in (("tom_high", "th", 0, "높은 탐"), ("tom_mid", "tm", 0, "가운데 탐"),
                                ("tom_floor", "tl", 0, "낮은 탐"), ("tom_low", "tl", -3, "낮은 탐을 3반음 내린 것")):
    sound(pad, "gogodze-toms", f"Gogodze {label} (70년대)", "gogodze-phu-vol-ii", "gogodze/toms",
          f"Samples/{{mic}}/{part}_vl{{v}}_rr{{r}}.wav", GOGODZE, pitch=pitch)


# ---------------------------------------------------------------------------------------------------
def run(args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(args, check=True, capture_output=True, text=True, encoding="utf-8", errors="replace")


def mic_gain(value) -> tuple[float, str]:
    """마이크 설정을 (배율, 놓을 자리)로. 자리는 "C"(가운데), "L", "R"."""
    return (value[0], value[1]) if isinstance(value, tuple) else (float(value), "C")


def discover(spec: dict) -> dict[int, list]:
    """기준 마이크(처음 적은 마이크)의 파일들에서 세기 단계 → 번갈아 재생 번호들을 찾는다."""
    library = LIBRARIES / spec["lib"]
    reference = next(iter(spec["mics"]))
    pattern = spec["files"].replace("{mic}", reference)
    expression = re.escape(pattern).replace(r"\{v\}", r"(?P<v>\d+)")

    if "{r}" in pattern:
        expression = expression.replace(r"\{r\}", r"(?P<r>\d+)", 1).replace(r"\{r\}", r"(?P=r)")

    matcher = re.compile("^" + expression + "$")
    found: dict[int, list] = {}

    for path in library.glob(pattern.replace("{v}", "*").replace("{r}", "*")):
        match = matcher.match(path.relative_to(library).as_posix())

        if match:
            found.setdefault(int(match.group("v")), []).append(int(match.group("r")) if "{r}" in pattern else None)

    return {level: sorted(numbers, key=lambda n: n or 0) for level, numbers in sorted(found.items())}


def pick_evenly(count: int, wanted: int) -> list[int]:
    if wanted >= count:
        return list(range(count))

    if wanted == 1:
        return [count - 1]

    return sorted({round(i * (count - 1) / (wanted - 1)) for i in range(wanted)})


def channels_of(path: Path) -> int:
    output = run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=channels",
                  "-of", "csv=p=0", str(path)]).stdout.strip()
    return int(output.split(",")[0] or 1)


# ---------------------------------------------------------------------------------------------------
# SFZ 프로그램을 흉내 내는 소리 (sfz_sound로 적은 것)
#
# 파일 이름에 세기와 번갈아 번호가 드러나지 않는 라이브러리는 그 라이브러리의 SFZ 프로그램을 읽는다.
# 건반 하나를 어떤 세기로 쳤을 때 함께 울리는 리전(마이크마다 하나씩일 수 있다)을 모두 찾아, 프로그램에 적힌
# 음량(volume, amplitude와 그 CC 조절)과 팬대로 섞는다. 그래서 마이크를 섞는 비율은 라이브러리의 기본 프로그램을 따른다.
# 세기 곡선(amp_veltrack, amp_velcurve)은 따르지 않는다: 녹음된 크기 그대로 두고, 세기에 따른 크기는 엔진이 맡는다.
# 읽는 것: #define, #include, <control>(default_path, set_ccN), <global>/<master>/<group>/<region>의 물려받기,
# key/lokey/hikey, lovel/hivel, loccN/hiccN, seq_length/seq_position, lorand/hirand, trigger, sample.

NOTE_STEPS = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
SFZ_CACHE: dict = {}


def note_number(text: str):
    """"36", "c4"(=60), "f#2" 같은 값을 건반 번호로."""
    text = text.strip().lower()

    if re.fullmatch(r"-?\d+", text):
        return int(text)

    match = re.fullmatch(r"([a-g])([#b]?)(-?\d+)", text)

    if not match:
        return None

    shift = 1 if match.group(2) == "#" else (-1 if match.group(2) == "b" else 0)
    return NOTE_STEPS[match.group(1)] + shift + (int(match.group(3)) + 1) * 12


def read_sfz_text(path: Path, root: Path, defines: dict, depth: int = 0) -> str:
    """#include를 펼치고 #define을 바꿔 넣은 글. include 경로는 처음 연 파일의 폴더 기준이고, 없으면 지금 파일의 폴더 기준."""
    if depth > 20:
        raise RuntimeError(f"#include too deep: {path}")

    text = re.sub(r"/\*.*?\*/", " ", path.read_text(encoding="utf-8", errors="replace"), flags=re.S)
    lines = []

    for line in text.splitlines():
        line = re.sub(r"(^|\s)//.*$", "", line)

        # 정의 줄은 이름을 바꿔 넣기 전에 알아본다. 같은 이름을 다시 정하는 일이 흔하다 (예: 악기마다 #define $instr Snare).
        # 한 줄에 정의가 여럿일 수도 있다 (예: #define $V51L 1   #define $V51H 27).
        if re.match(r"\s*#define\s", line):
            for segment in re.split(r"#define\s+", line)[1:]:
                parts = segment.strip().split(None, 1)

                if len(parts) != 2 or not parts[0].startswith("$"):
                    continue

                value = parts[1].strip()

                for name in sorted(defines, key=len, reverse=True):
                    value = value.replace(name, defines[name])

                defines[parts[0]] = value

            continue

        for name in sorted(defines, key=len, reverse=True):
            line = line.replace(name, defines[name])

        # 한 줄에 #include가 여럿 있을 수 있다 (예: #include "global.txt" #include "kick.txt").
        parts = re.split(r'#include\s+"([^"]+)"', line)

        for index, part in enumerate(parts):
            if index % 2 == 0:
                if part.strip():
                    lines.append(part)
                continue

            name = part.replace("\\", "/")
            target = root / name if (root / name).exists() else path.parent / name
            lines.append(read_sfz_text(target, root, defines, depth + 1))

    return "\n".join(lines)


def parse_sfz(program: Path):
    """SFZ 파일의 리전 목록(물려받은 값을 모두 합친 것)과 <control>의 CC 기본값."""
    if program in SFZ_CACHE:
        return SFZ_CACHE[program]

    text = read_sfz_text(program, program.parent, {})
    blocks = []
    tokens = list(re.finditer(r"<(\w+)>|([A-Za-z_]\w*)=", text))

    for index, token in enumerate(tokens):
        end = tokens[index + 1].start() if index + 1 < len(tokens) else len(text)

        if token.group(1):
            blocks.append((token.group(1).lower(), {}))
        elif blocks:
            blocks[-1][1][token.group(2).lower()] = text[token.end():end].strip()

    control, global_, master, group = {}, {}, {}, {}
    cc_defaults = {}
    regions = []

    for header, opcodes in blocks:
        # 키 스위치의 처음 값. 어느 머리글에 적혀 있어도 프로그램 전체의 처음 상태로 본다.
        if "sw_default" in opcodes:
            try:
                cc_defaults["sw"] = float(note_number(opcodes["sw_default"]) or float(opcodes["sw_default"].split()[0]))
            except (ValueError, IndexError):
                pass

        if header == "control":
            control.update(opcodes)

            for name, value in opcodes.items():
                match = re.fullmatch(r"set_(hd)?cc(\d+)", name)

                if match:
                    try:
                        number = float(value.split()[0])
                    except (ValueError, IndexError):
                        continue

                    # set_hdcc는 0 ~ 1로 적는다. 여기서는 모두 0 ~ 127로 다룬다.
                    cc_defaults[int(match.group(2))] = number * 127.0 if match.group(1) else number
        elif header == "global":
            global_, master, group = dict(opcodes), {}, {}
        elif header == "master":
            master, group = dict(opcodes), {}
        elif header == "group":
            group = dict(opcodes)
        elif header == "region":
            region = {**global_, **master, **group, **opcodes}
            region["_default_path"] = control.get("default_path", "")
            regions.append(region)

    SFZ_CACHE[program] = (regions, cc_defaults)
    return SFZ_CACHE[program]


def sfz_number(region: dict, name: str, default: float) -> float:
    value = region.get(name)

    if value is None:
        return default

    if name in ("key", "lokey", "hikey"):
        number = note_number(value)

        if number is not None:
            return float(number)

    try:
        return float(value.split()[0])
    except (ValueError, IndexError):
        return default


def sfz_regions_at(program: Path, key: int, velocity: int, sequence: int, cc: dict) -> list:
    """key를 velocity로 칠 때 울리는 리전들. sequence는 seq_position으로 번갈아 재생할 때 몇 번째인지(1부터)."""
    regions, cc_defaults = parse_sfz(program)
    values = {**cc_defaults, **cc}
    playing = []

    for region in regions:
        if "key" in region:
            low = high = sfz_number(region, "key", -1)
        else:
            low, high = sfz_number(region, "lokey", 0), sfz_number(region, "hikey", 127)

        if not low <= key <= high:
            continue

        if not sfz_number(region, "lovel", 1) <= velocity <= sfz_number(region, "hivel", 127):
            continue

        if region.get("trigger", "attack") not in ("attack", "first", "legato"):
            continue

        sample = region.get("sample", "")

        if not sample or sample.startswith("*"):
            continue

        allowed = True

        for name, value in region.items():
            match = re.fullmatch(r"(lo|hi)(hd)?cc(\d+)", name)

            if match:
                try:
                    limit = float(value.split()[0])
                except (ValueError, IndexError):
                    continue    # 정의되지 않은 $값 같은 것: 원래 프로그램(sforzando)도 이 조건을 무시한다

                current = values.get(int(match.group(3)), 0.0) / (127.0 if match.group(2) else 1.0)

                if (match.group(1) == "lo" and current < limit) or (match.group(1) == "hi" and current > limit):
                    allowed = False

        if not allowed:
            continue

        if sfz_number(region, "seq_length", 1) > 1 and int(sfz_number(region, "seq_position", 1)) != sequence:
            continue

        # 키 스위치: 지금 눌린 스위치(처음에는 sw_default)에 맞는 리전만 운다. 처음 값이 없으면 따지지 않는다.
        switch = values.get("sw")

        if switch is not None:
            if "sw_last" in region and sfz_number(region, "sw_last", -1) != switch:
                continue

            if ("sw_lolast" in region or "sw_hilast" in region) and not (
                    sfz_number(region, "sw_lolast", 0) <= switch <= sfz_number(region, "sw_hilast", 127)):
                continue

        playing.append(region)

    return playing


def sfz_variants(program: Path, key: int, velocity: int, cc: dict) -> list:
    """번갈아 재생의 갈래들. seq_length면 1..길이, lorand/hirand면 구간마다 그 가운데 값, 둘 다 없으면 [1]."""
    regions = sfz_regions_at(program, key, velocity, 1, cc)
    length = max((int(sfz_number(region, "seq_length", 1)) for region in regions), default=1)

    if length > 1:
        return list(range(1, length + 1))

    bounds = sorted({0.0, 1.0} | {sfz_number(r, "lorand", 0.0) for r in regions} | {sfz_number(r, "hirand", 1.0) for r in regions})

    if len(bounds) > 2:
        return [(bounds[i] + bounds[i + 1]) / 2.0 for i in range(len(bounds) - 1)]

    return [1]


def sfz_mix(program: Path, key: int, velocity: int, variant, cc: dict) -> list:
    """한 번 칠 때 섞을 소리들: 파일, 배율, 팬(-1 ~ 1)."""
    regions, cc_defaults = parse_sfz(program)
    values = {**cc_defaults, **cc}
    sequence = variant if isinstance(variant, int) else 1
    sources = []

    for region in sfz_regions_at(program, key, velocity, sequence, cc):
        if isinstance(variant, float) and not sfz_number(region, "lorand", 0.0) <= variant < sfz_number(region, "hirand", 1.0):
            continue

        decibels = sfz_number(region, "volume", 0.0)
        amplitude = sfz_number(region, "amplitude", 100.0) / 100.0
        pan = sfz_number(region, "pan", 0.0)

        for name, value in region.items():
            match = re.fullmatch(r"(volume|amplitude|pan)_(?:on)?cc(\d+)", name)

            if not match:
                continue

            try:
                depth = float(value.split()[0])
            except (ValueError, IndexError):
                continue

            # CC 값을 곡선에 넣은 값만큼 조절한다 (ARIA의 기본 곡선. 적지 않으면 0번, 곧은 0 ~ 1).
            # 팬은 흔히 1번(가운데 64가 0인 -1 ~ 1)을 쓰고, 곧게 셈하면 오른쪽으로 쏠린다.
            position = max(0.0, min(1.0, values.get(int(match.group(2)), 0.0) / 127.0))
            curve = int(sfz_number(region, f"{match.group(1)}_curvecc{match.group(2)}", 0))
            shaped = {0: position, 1: position * 2.0 - 1.0, 2: 1.0 - position, 3: 1.0 - position * 2.0,
                      4: position * position, 5: position ** 0.5, 6: (1.0 - position) ** 0.5}.get(curve, position)
            amount = depth * shaped

            if match.group(1) == "volume":
                decibels += amount
            elif match.group(1) == "amplitude":
                amplitude *= max(0.0, amount) / 100.0
            else:
                pan += amount

        gain = amplitude * 10 ** (decibels / 20.0)
        sample = (region["_default_path"] + region["sample"]).replace("\\", "/")
        file = (program.parent / sample).resolve()

        if gain > 1.0e-4 and file.exists():
            sources.append({"path": file, "gain": round(gain, 5), "pan": max(-1.0, min(1.0, pan / 100.0))})

    return sources


def plan_sfz(spec: dict) -> list:
    """SFZ 프로그램을 흉내 내는 소리의 타격 목록."""
    program = LIBRARIES / spec["lib"] / spec["program"]
    key, cc = spec["key"], spec.get("cc", {})
    layers_wanted, rr_wanted, max_seconds = SHAPES[spec["pad"]]

    # 세기 1 ~ 127을 훑어, 울리는 파일들이 같은 세기끼리 묶는다. 묶음마다 가장 센 세기로 친다.
    zones, previous = [], None

    for velocity in range(1, 128):
        signature = tuple(sorted(str(source["path"]) for source in sfz_mix(program, key, velocity, 1, cc)))

        if not signature:
            previous = None
            continue

        if signature == previous:
            zones[-1] = velocity
        else:
            zones.append(velocity)

        previous = signature

    if not zones:
        raise RuntimeError(f"nothing plays on key {key} in {spec['program']}")

    if len(zones) > layers_wanted:
        zones = zones[len(zones) // 5:]    # 가장 여린 단계들은 거의 들리지 않는다

    if len(sfz_variants(program, key, zones[-1], cc)) == 1 and len(zones) > layers_wanted:
        # 번갈아 재생할 샘플이 따로 없는 프로그램: 세기 단계들을 묶음으로 나누고, 묶음마다 센 쪽의 몇 단계를 번갈아 낸다.
        bounds = [round(i * len(zones) / layers_wanted) for i in range(layers_wanted + 1)]
        groups = [list(reversed(zones[bounds[i]:bounds[i + 1]][-rr_wanted:])) for i in range(layers_wanted)]
    else:
        groups = [[zones[i]] for i in pick_evenly(len(zones), layers_wanted)]

    chosen = groups
    hits = []

    for layer_number, velocities in enumerate(groups, start=1):
        if len(velocities) > 1:
            takes = [(velocity, 1) for velocity in velocities]
        else:
            takes = [(velocities[0], variant) for variant in sfz_variants(program, key, velocities[0], cc)[:rr_wanted]]

        for rr_number, (velocity, variant) in enumerate(takes, start=1):
            sources = sfz_mix(program, key, velocity, variant, cc)

            if not sources:
                continue

            hits.append({
                "layer": layer_number,
                "up_to": 1.0 if layer_number == len(chosen) else round(layer_number / len(chosen), 4),
                "output": f"samples/{spec['pad']}/{spec['id']}/{spec['id']}_v{layer_number:02d}_r{rr_number}.flac",
                "sources": sources,
                "max_seconds": max_seconds,
                "pitch": spec.get("pitch", 0),
                "top": layer_number == len(chosen),
            })

    return hits


def sfz_sound(pad: str, sound_id: str, name: str, lib: str, family: str, program: str, key: int, cc=None, **extra) -> None:
    """SFZ 프로그램을 흉내 내는 소리를 적는다. program은 라이브러리 폴더 기준 경로, key는 건반 번호, cc는 바꿔 둘 CC 값."""
    if pad in SINGLE_PADS:
        family = ""

    SOUNDS.append(dict(pad=pad, id=sound_id, name=name, lib=lib, family=family, program=program, key=key, cc=cc or {}, **extra))


def plan(spec: dict) -> list[dict]:
    """소리 하나에 쓸 타격 목록. 타격마다 섞을 마이크 파일들을 적는다."""
    if "program" in spec:
        return plan_sfz(spec)

    library = LIBRARIES / spec["lib"]
    layers_wanted, rr_wanted, max_seconds = SHAPES[spec["pad"]]
    levels = discover(spec)

    if not levels:
        raise RuntimeError(f"no files for {spec['pad']}/{spec['id']}: {spec['files']}")

    keys = list(levels)

    # 세기 단계가 넉넉한 라이브러리의 가장 여린 단계들은 거의 들리지 않을 만큼 작다. 아래쪽 5분의 1은 쓰지 않는다.
    if len(keys) > layers_wanted:
        keys = keys[len(keys) // 5:]

    if "{r}" in spec["files"]:
        # (세기 단계 번호, 원본의 세기 단계, 번갈아 재생 번호)
        chosen = [(number, keys[i], rr)
                  for number, i in enumerate(pick_evenly(len(keys), layers_wanted), start=1)
                  for rr in levels[keys[i]][:rr_wanted]]
    else:
        # 번갈아 재생할 파일이 따로 없는 라이브러리는 세기 단계가 촘촘하다. 단계들을 묶음으로 나누고,
        # 묶음마다 센 쪽의 몇 단계를 번갈아 낸다.
        count = min(layers_wanted, len(keys))
        bounds = [round(i * len(keys) / count) for i in range(count + 1)]
        chosen = [(number, level, None)
                  for number in range(1, count + 1)
                  for level in reversed(keys[bounds[number - 1]:bounds[number]][-rr_wanted:])]

    num_layers = max(number for number, _, _ in chosen)
    rr_counter: dict[int, int] = {}
    hits = []

    for layer_number, level, rr in chosen:
        rr_number = rr_counter[layer_number] = rr_counter.get(layer_number, 0) + 1
        sources = []

        for mic, value in spec["mics"].items():
            gain, place = mic_gain(value)
            path = library / spec["files"].format(mic=mic, v=level, r=rr)

            if gain > 0 and path.exists():
                sources.append({"path": path, "gain": gain, "place": place})

        if not sources:
            raise RuntimeError(f"no mic files for {spec['pad']}/{spec['id']} level {level}")

        hits.append({
            "layer": layer_number,
            "up_to": 1.0 if layer_number == num_layers else round(layer_number / num_layers, 4),
            "output": f"samples/{spec['pad']}/{spec['id']}/{spec['id']}_v{layer_number:02d}_r{rr_number}.flac",
            "sources": sources,
            "max_seconds": max_seconds,
            "pitch": spec.get("pitch", 0),
            "top": layer_number == num_layers,
        })

    return hits


def filter_graph(hit: dict, tail: str) -> str:
    chains, labels = [], []

    for index, source in enumerate(hit["sources"]):
        if "channels" not in source:
            source["channels"] = channels_of(source["path"])

        if "pan" in source:
            # SFZ의 팬: 가운데는 양쪽 그대로, 한쪽으로 갈수록 반대쪽을 줄인다.
            left_gain, right_gain = min(1.0, 1.0 - source["pan"]), min(1.0, 1.0 + source["pan"])

            if source["channels"] >= 2:
                shape = f"aformat=channel_layouts=stereo,pan=stereo|c0={left_gain:.4f}*c0|c1={right_gain:.4f}*c1,"
            else:
                shape = f"pan=stereo|c0={left_gain:.4f}*c0|c1={right_gain:.4f}*c0,"
        elif source["channels"] >= 2:
            shape = "aformat=channel_layouts=stereo,"
        else:
            # 모노 마이크는 가운데, 왼쪽, 오른쪽 가운데 한 자리에 놓는다.
            left, right = {"C": ("c0", "c0"), "L": ("c0", "0*c0"), "R": ("0*c0", "c0")}[source["place"]]
            shape = f"pan=stereo|c0={left}|c1={right},"

        chains.append(f"[{index}:a]{shape}volume={source['gain']}[m{index}]")
        labels.append(f"[m{index}]")

    if len(labels) > 1:
        chains.append(f"{''.join(labels)}amix=inputs={len(labels)}:normalize=0:duration=longest[mix]")
        current = "[mix]"
    else:
        current = labels[0]

    post = []

    if hit["pitch"]:
        # 음높이를 내리면 그만큼 길어진다. 드럼 한 타라서 길이가 조금 달라져도 괜찮다.
        post.append(f"aresample={SAMPLE_RATE}:resampler=soxr")
        post.append(f"asetrate={SAMPLE_RATE * 2 ** (hit['pitch'] / 12.0):.3f}")

    post.append(f"aresample={SAMPLE_RATE}:resampler=soxr:precision=28")

    if hit["max_seconds"]:
        end = hit["max_seconds"]
        post.append(f"atrim=end={end}")
        post.append(f"afade=t=out:st={end - FADE_OUT_SECONDS}:d={FADE_OUT_SECONDS}")

    post.append(tail)
    chains.append(f"{current}{','.join(post)}[out]")
    return ";".join(chains)


def inputs_of(hit: dict) -> list[str]:
    args = []

    for source in hit["sources"]:
        args += ["-i", str(source["path"])]

    return args


def peak_db(args: list[str]) -> float:
    result = run(["ffmpeg", "-hide_banner", "-nostdin", *args, "-f", "null", "-"])
    match = re.search(r"max_volume:\s*(-?[\d.]+) dB", result.stderr)

    if not match:
        raise RuntimeError("could not measure a peak")

    return float(match.group(1))


def measure_hit(hit: dict) -> float:
    return peak_db([*inputs_of(hit), "-filter_complex", filter_graph(hit, "volumedetect"), "-map", "[out]"])


def render_hit(kit_folder: Path, hit: dict, scale: float) -> None:
    target = kit_folder / hit["output"]
    target.parent.mkdir(parents=True, exist_ok=True)
    run(["ffmpeg", "-hide_banner", "-nostdin", "-v", "error", "-y", *inputs_of(hit),
         "-filter_complex", filter_graph(hit, f"volume={scale:.6f}"), "-map", "[out]",
         "-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24", str(target)])


def layers_json(hits: list[dict]) -> list[dict]:
    layers: dict[int, dict] = {}

    for hit in hits:
        layers.setdefault(hit["layer"], {"upTo": hit["up_to"], "samples": []})["samples"].append(hit["output"])

    return [layers[number] for number in sorted(layers)]


def loudness_of(kit_folder: Path, layers: list[dict]) -> float:
    """패드에 지금 들어 있는 소리의 크기: 가장 센 단계의 타격들의 최댓값(dB)의 가운뎃값."""
    top = max(layers, key=lambda layer: layer.get("upTo", 1.0))
    return statistics.median(peak_db(["-i", str(kit_folder / sample), "-af", "volumedetect"]) for sample in top["samples"])


def keep_backup(path: Path) -> None:
    """kit.json을 다시 쓰기 전에 지금 내용을 backups/에 남긴다.

    프로그램이 남기는 백업과 같은 자리, 같은 이름 규칙이다 (src/app/Backups.h). 그래서 킷 화면의 "되돌리기…"에 함께 보인다.
    """
    if not path.exists():
        return

    root = Path(__file__).resolve().parent.parent

    try:
        name = "__".join(path.resolve().relative_to(root).parts)
    except ValueError:
        name = path.name

    folder = root / "backups" / name
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / (datetime.now().strftime("%Y%m%d-%H%M%S") + path.suffix)

    if not target.exists():
        shutil.copy2(path, target)



# ===================================================================================================
# 아래는 CC0가 아닌 라이브러리다(2026-11-26, 사용자가 받기로 함). 출처를 README와 kits/default/CREDITS.txt에 적는다.
# 모두 SFZ 프로그램을 흉내 내어 만든다(sfz_sound). 마이크를 섞는 비율은 각 라이브러리의 기본 프로그램 그대로다.

# ---------------------------------------------------------------------------------------------------
# Naked Drums (Wilkinson Audio, CC-BY 4.0): 마이크 일곱까지, 번갈아 10번
NAKED = "Wilkinson Audio/Naked Drums/Stereo/Naked Drums.sfz"
sfz_sound("kick", "naked", "Naked Drums 킥", "naked-drums", "", NAKED, 36)

for pad, key in (("snare", 38), ("snare_rim", 40), ("sidestick", 37)):
    sfz_sound(pad, "naked", "Naked Drums 스네어", "naked-drums", "naked/snare", NAKED, key)

for pad, key in (("hihat_closed", 42), ("hihat_half", 46), ("hihat_open", 49), ("hihat_pedal", 44)):
    sfz_sound(pad, "naked", "Naked Drums 하이햇", "naked-drums", "naked/hihat", NAKED, key)

sfz_sound("hihat_closed", "naked-tight", "Naked Drums 하이햇 (꽉 닫음)", "naked-drums", "", NAKED, 39)

for pad, key in (("tom_high", 48), ("tom_mid", 45), ("tom_floor", 43), ("tom_low", 41)):
    sfz_sound(pad, "naked-toms", "Naked Drums 탐", "naked-drums", "naked/toms", NAKED, key)

for pad in ("crash1", "crash2"):
    sfz_sound(pad, "naked-crash1", "Naked Drums 크래시 1", "naked-drums", "", NAKED, 55)
    sfz_sound(pad, "naked-crash2", "Naked Drums 크래시 2", "naked-drums", "", NAKED, 57)
    sfz_sound(pad, "naked-splash", "Naked Drums 스플래시", "naked-drums", "", NAKED, 54)

sfz_sound("china", "naked-china1", "Naked Drums 차이나 1", "naked-drums", "", NAKED, 50)
sfz_sound("china", "naked-china2", "Naked Drums 차이나 2", "naked-drums", "", NAKED, 52)

for pad, key in (("ride", 51), ("ride_bell", 53)):
    sfz_sound(pad, "naked", "Naked Drums 라이드", "naked-drums", "naked/ride", NAKED, key)

# ---------------------------------------------------------------------------------------------------
# DRS Kit (DrumGizmo, CC-BY 4.0): 마이크 열셋. 키 스위치는 기본(주법마다 건반이 따로)이라 브러시 소리는 섞이지 않는다.
DRS = "DrumGizmo/DRSKit/Stereo/DrumGizmo DRSKit.sfz"
sfz_sound("kick", "drs", "DRS 킥", "drs-kit", "", DRS, 35)
sfz_sound("kick", "drs-contact", "DRS 킥 (비터를 붙인 채)", "drs-kit", "", DRS, 36)

for pad, key in (("snare", 38), ("snare_rim", 37)):
    sfz_sound(pad, "drs", "DRS 스네어", "drs-kit", "drs/snare", DRS, key)

for pad, key in (("hihat_closed", 56), ("hihat_half", 46), ("hihat_open", 58), ("hihat_pedal", 44)):
    sfz_sound(pad, "drs", "DRS 하이햇", "drs-kit", "drs/hihat", DRS, key)

for pad, key, pitch, label in (("tom_high", 47, 0, "랙 탐"), ("tom_mid", 43, 0, "플로어 탐 1"), ("tom_floor", 41, 0, "플로어 탐 2"),
                               ("tom_low", 41, -3, "플로어 탐 2를 3반음 내린 것")):
    sfz_sound(pad, "drs-toms", f"DRS {label}", "drs-kit", "drs/toms", DRS, key, pitch=pitch)

for pad in ("crash1", "crash2"):
    sfz_sound(pad, "drs-crash-left", "DRS 왼쪽 크래시", "drs-kit", "", DRS, 49)
    sfz_sound(pad, "drs-crash-right", "DRS 오른쪽 크래시", "drs-kit", "", DRS, 57)

for pad, key in (("ride", 51), ("ride_bell", 53), ("ride_edge", 66)):
    sfz_sound(pad, "drs", "DRS 라이드", "drs-kit", "drs/ride", DRS, key)

# ---------------------------------------------------------------------------------------------------
# Muldjord Kit (DrumGizmo, CC-BY 4.0): 탐 넷, 라이드 둘. 하이햇은 닫힘과 열림만 있다.
MULDJORD = "DrumGizmo/MuldjordKit/Stereo/DrumGizmo MuldjordKit.sfz"
sfz_sound("kick", "muldjord", "Muldjord 킥", "muldjord-kit", "", MULDJORD, 36)
sfz_sound("snare", "muldjord", "Muldjord 스네어", "muldjord-kit", "muldjord/snare", MULDJORD, 38)

for pad, key in (("hihat_closed", 42), ("hihat_open", 46)):
    sfz_sound(pad, "muldjord", "Muldjord 하이햇", "muldjord-kit", "muldjord/hihat", MULDJORD, key)

for pad, key in (("tom_high", 48), ("tom_mid", 47), ("tom_floor", 45), ("tom_low", 41)):
    sfz_sound(pad, "muldjord-toms", "Muldjord 탐", "muldjord-kit", "muldjord/toms", MULDJORD, key)

for pad in ("crash1", "crash2"):
    sfz_sound(pad, "muldjord-crash-left", "Muldjord 왼쪽 크래시", "muldjord-kit", "", MULDJORD, 49)
    sfz_sound(pad, "muldjord-crash-right", "Muldjord 오른쪽 크래시", "muldjord-kit", "", MULDJORD, 57)

sfz_sound("china", "muldjord", "Muldjord 차이나", "muldjord-kit", "", MULDJORD, 52)

for pad, key in (("ride", 51), ("ride_bell", 53)):
    sfz_sound(pad, "muldjord", "Muldjord 오른쪽 라이드", "muldjord-kit", "muldjord/ride", MULDJORD, key)

for pad, key in (("ride", 59), ("ride_bell", 55)):
    sfz_sound(pad, "muldjord-left", "Muldjord 왼쪽 라이드", "muldjord-kit", "muldjord/ride-left", MULDJORD, key)

# ---------------------------------------------------------------------------------------------------
# Sam's Sonor (Sam Greene, CC-BY-SA 4.0): Sonor Force 3001. 스테레오 한 파일, 탐 둘.
SONOR = "SamsSonor.sfz"
sfz_sound("kick", "sonor", "Sonor 킥", "sams-sonor", "", SONOR, 36)
sfz_sound("snare", "sonor", "Sonor 스네어", "sams-sonor", "sonor/snare", SONOR, 38)
sfz_sound("snare", "sonor-off", "Sonor 스네어 (와이어를 푼 것)", "sams-sonor", "", SONOR, 48)

for pad, key in (("hihat_closed", 42), ("hihat_half", 56), ("hihat_open", 58), ("hihat_pedal", 44)):
    sfz_sound(pad, "sonor", "Sonor 하이햇", "sams-sonor", "sonor/hihat", SONOR, key)

for pad, key, pitch, label in (("tom_high", 45, 0, "높은 탐"), ("tom_mid", 45, -3, "높은 탐을 3반음 내린 것"),
                               ("tom_floor", 43, 0, "낮은 탐"), ("tom_low", 43, -3, "낮은 탐을 3반음 내린 것")):
    sfz_sound(pad, "sonor-toms", f"Sonor {label}", "sams-sonor", "sonor/toms", SONOR, key, pitch=pitch)

for pad in ("crash1", "crash2"):
    sfz_sound(pad, "sonor-crash", "Sonor 크래시", "sams-sonor", "", SONOR, 49)

for pad, key in (("ride", 51), ("ride_bell", 53)):
    sfz_sound(pad, "sonor", "Sonor 라이드", "sams-sonor", "sonor/ride", SONOR, key)

# ---------------------------------------------------------------------------------------------------
# AVL Drumkits (CC-BY-SA 3.0): Black Pearl과 Red Zeppelin. 스테레오 한 파일, 세기 다섯 단계.
for kit_id, label, program, toms, crashes in (
        ("avl-pearl", "AVL Black Pearl", "Black_Pearl_5pc.sfz",
         ((45, 0, "12인치 탐"), (47, 0, "13인치 탐"), (41, 0, "16인치 플로어 탐"), (41, -3, "16인치 플로어 탐을 3반음 내린 것")),
         ((49, "Sabian AA 16인치"), (57, "Sabian AAX 17인치"))),
        ("avl-zeppelin", "AVL Red Zeppelin", "Red_Zeppelin_5pc.sfz",
         ((45, 0, "14인치 탐"), (41, 0, "16인치 플로어 탐"), (43, 0, "18인치 플로어 탐"), (43, -3, "18인치 플로어 탐을 3반음 내린 것")),
         ((49, "Zildjian A 20인치"), (57, "Zildjian K 17인치")))):
    sfz_sound("kick", kit_id, f"{label} 킥", "avl-drumkits", "", program, 36)

    for pad, key in (("snare", 38), ("sidestick", 37)):
        sfz_sound(pad, kit_id, f"{label} 스네어", "avl-drumkits", f"{kit_id}/snare", program, key)

    for pad, key in (("hihat_closed", 42), ("hihat_half", 46), ("hihat_open", 48), ("hihat_pedal", 44)):
        sfz_sound(pad, kit_id, f"{label} 하이햇", "avl-drumkits", f"{kit_id}/hihat", program, key)

    for pad, (key, pitch, name) in zip(("tom_high", "tom_mid", "tom_floor", "tom_low"), toms):
        sfz_sound(pad, f"{kit_id}-toms", f"{label} {name}", "avl-drumkits", f"{kit_id}/toms", program, key, pitch=pitch)

    for pad in ("crash1", "crash2"):
        for number, (key, name) in enumerate(crashes, start=1):
            sfz_sound(pad, f"{kit_id}-crash{number}", f"{label} {name} 크래시", "avl-drumkits", "", program, key)

    for pad, key in (("ride", 51), ("ride_bell", 53), ("ride_edge", 59)):
        sfz_sound(pad, kit_id, f"{label} 라이드", "avl-drumkits", f"{kit_id}/ride", program, key)

# ---------------------------------------------------------------------------------------------------
# Salamander Drumkit (Alexander Holm, CC-BY-SA 3.0): 오버헤드 스테레오 한 파일, 번갈아 많이. 탐 둘.
SALAMANDER = "ALL.sfz"
sfz_sound("kick", "salamander", "Salamander 킥", "salamander-drumkit", "", SALAMANDER, 36)
sfz_sound("kick", "salamander-2", "Salamander 킥 2", "salamander-drumkit", "", SALAMANDER, 35)

for pad, key in (("snare", 40), ("sidestick", 41)):
    sfz_sound(pad, "salamander", "Salamander 스네어", "salamander-drumkit", "salamander/snare", SALAMANDER, key)

sfz_sound("snare", "salamander-2", "Salamander 스네어 2", "salamander-drumkit", "", SALAMANDER, 38)
sfz_sound("snare", "salamander-off", "Salamander 스네어 (와이어를 푼 것)", "salamander-drumkit", "", SALAMANDER, 39)

for pad, key in (("hihat_closed", 42), ("hihat_open", 46), ("hihat_pedal", 44)):
    sfz_sound(pad, "salamander", "Salamander 하이햇", "salamander-drumkit", "salamander/hihat", SALAMANDER, key)

for pad, key, pitch, label in (("tom_high", 45, 0, "높은 탐"), ("tom_mid", 45, -3, "높은 탐을 3반음 내린 것"),
                               ("tom_floor", 43, 0, "낮은 탐"), ("tom_low", 43, -3, "낮은 탐을 3반음 내린 것")):
    sfz_sound(pad, "salamander-toms", f"Salamander {label}", "salamander-drumkit", "salamander/toms", SALAMANDER, key, pitch=pitch)

for pad in ("crash1", "crash2"):
    for number, key in ((1, 55), (2, 57), (3, 62)):
        sfz_sound(pad, f"salamander-crash{number}", f"Salamander 크래시 {number}", "salamander-drumkit", "", SALAMANDER, key)

    sfz_sound(pad, "salamander-splash", "Salamander 스플래시", "salamander-drumkit", "", SALAMANDER, 63)

sfz_sound("china", "salamander-china1", "Salamander 차이나 1", "salamander-drumkit", "", SALAMANDER, 59)
sfz_sound("china", "salamander-china2", "Salamander 차이나 2", "salamander-drumkit", "", SALAMANDER, 60)

for pad, key in (("ride", 48), ("ride_bell", 49), ("ride_edge", 50)):
    sfz_sound(pad, "salamander", "Salamander 라이드", "salamander-drumkit", "salamander/ride", SALAMANDER, key)

for pad, key in (("ride", 52), ("ride_bell", 53)):
    sfz_sound(pad, "salamander-ride1", "Salamander 라이드 1", "salamander-drumkit", "salamander/ride1", SALAMANDER, key)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--kit", type=Path, default=ROOT / "kits" / "default", help="소리를 넣을 킷 폴더")
    parser.add_argument("--only", default="", help="쉼표로 나눈 패드 id. 이 패드의 소리만 만든다")
    parser.add_argument("--list", action="store_true", help="만들 수 있는 소리를 보여 주기만 한다")
    parser.add_argument("--force", action="store_true", help="이미 있는 소리도 다시 만든다")
    options = parser.parse_args()

    only = {name.strip() for name in options.only.split(",") if name.strip()}
    kit_folder = options.kit.resolve()
    kit_path = kit_folder / "kit.json"
    kit = json.loads(kit_path.read_text(encoding="utf-8"))
    keep_backup(kit_path)    # 아래에서 소리 하나를 만들 때마다 kit.json을 다시 쓴다
    pads = {pad["id"]: pad for pad in kit["pads"]}
    workers = max(2, (os.cpu_count() or 4) // 2)

    specs = [spec for spec in SOUNDS if (not only or spec["pad"] in only)]
    missing_libraries = sorted({spec["lib"] for spec in specs if not (LIBRARIES / spec["lib"] / ".complete").exists()})

    if missing_libraries:
        print(f"받지 않은 라이브러리의 소리는 건너뜁니다: {', '.join(missing_libraries)} (pwsh scripts/fetch-samples.ps1)")
        specs = [spec for spec in specs if spec["lib"] not in missing_libraries]

    if options.list:
        for spec in specs:
            if "program" in spec:
                hits = plan_sfz(spec)
                layers = max((hit["layer"] for hit in hits), default=0)
                mics = max((len(hit["sources"]) for hit in hits), default=0)
                print(f"{spec['pad']:13s} {spec['id']:22s} 세기 {layers:2d}단계, 번갈아 {len(hits) // max(1, layers)}, 섞는 파일 {mics}  {spec['name']}")
            else:
                levels = discover(spec)
                print(f"{spec['pad']:13s} {spec['id']:22s} 세기 {len(levels):2d}단계  {spec['name']}")

        print(f"소리 {len(specs)}개")
        return 0

    # 패드마다 원래 소리를 목록의 맨 앞에 적어 둔다. 음량의 기준도 이 소리다.
    targets: dict[str, float] = {}

    for pad_id, (name, family) in ORIGINALS.items():
        pad = pads.get(pad_id)

        if pad is None or (only and pad_id not in only):
            continue

        sounds = pad.setdefault("sounds", [])
        original = next((entry for entry in sounds if entry.get("id") == "original"), None)

        if original is None:
            if "sound" in pad or "layers" not in pad:
                print(f"{pad_id}: 원래 소리를 알 수 없어 건너뜁니다 (이미 다른 소리로 바꿨거나 샘플을 직접 골랐습니다)")
                continue

            original = {"id": "original", "name": name, "family": family, "layers": json.loads(json.dumps(pad["layers"]))}
            sounds.insert(0, original)
            pad["sound"] = "original"

        targets[pad_id] = loudness_of(kit_folder, original["layers"])

    made = 0

    for spec in specs:
        pad = pads.get(spec["pad"])

        if pad is None or spec["pad"] not in targets:
            continue

        sounds = pad["sounds"]
        existing = next((entry for entry in sounds if entry.get("id") == spec["id"]), None)

        try:
            hits = plan(spec)
        except RuntimeError as error:
            print(f"{spec['pad']:13s} {spec['id']:22s} 건너뜀: {error}", flush=True)
            continue

        if existing is not None and not options.force and all((kit_folder / hit["output"]).exists() for hit in hits):
            continue

        with ThreadPoolExecutor(max_workers=workers) as pool:
            peaks = list(pool.map(measure_hit, [hit for hit in hits if hit["top"]]))

            # 가운뎃값을 원래 소리에 맞추되, 가장 큰 타격이 넘치지 않게 한다.
            gain_db = min(targets[spec["pad"]] - statistics.median(peaks), -0.5 - max(peaks))
            scale = 10 ** (gain_db / 20.0)
            list(pool.map(lambda hit: render_hit(kit_folder, hit, scale), hits))

        entry = {"id": spec["id"], "name": spec["name"], "family": spec["family"],
                 "source": spec["lib"], "layers": layers_json(hits)}

        if existing is not None:
            existing.clear()
            existing.update(entry)
        else:
            sounds.append(entry)

        made += 1
        print(f"{spec['pad']:13s} {spec['id']:22s} 타격 {len(hits):2d}개  {20 * math.log10(scale):+6.1f} dB  {spec['name']}", flush=True)

        # 중간에 멈춰도 만든 것은 남도록 소리 하나마다 저장한다.
        temporary = kit_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(kit, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        temporary.replace(kit_path)

    temporary = kit_path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(kit, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(kit_path)

    total = sum(len(pad.get("sounds", [])) for pad in kit["pads"])
    print(f"완료: 이번에 만든 소리 {made}개, 킷에 모아 둔 소리 모두 {total}개 → {kit_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
