#!/usr/bin/env python3
"""모아 둔 소리를 만든다.

external/sample-libraries 에 받아 둔 드럼 라이브러리(모두 CC0)에서 악기별 소리를 골라, 마이크별로 나뉜
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
UNRULY_13 = {"top": 0.63, "btm": 0.31, "ohl": (0.87, "L"), "ohr": (0.87, "R")}
UNRULY_14 = {"top": 0.63, "btm": 0.63, "ohl": (0.79, "L"), "ohr": (0.79, "R")}
UNRULY_22 = {"top": 0.63, "btm": 0.79, "ohl": (0.87, "L"), "ohr": (0.87, "R")}
# Unruly의 기본 프로그램은 킥의 안쪽 마이크와 심벌·하이햇의 가까운 마이크를 꺼 둔다(오버헤드만 쓴다).
# 여기서는 타격이 또렷하도록 조금 섞는다.
UNRULY_KICK = {"out": 0.79, "in": 0.40, "oh": 0.31}
UNRULY_HAT = {"cl": 0.30, "ohl": (0.63, "L"), "ohr": (0.63, "R")}
UNRULY_CYMBAL = {"cl": 0.25, "oh": 0.63}
ONE_FILE = {"-": 1.0}    # 마이크가 나뉘지 않은 소리
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
# Unruly Drums: 모든 드럼에 스네어 와이어가 달린 킷
for size, mics in (("13", UNRULY_13), ("14", UNRULY_14)):
    for pad, part in (("snare", "center"), ("snare_rim", "rimshot"), ("sidestick", "sstick")):
        sound(pad, f"unruly-s{size}", f"Unruly {size}인치", "unruly-drums", f"unruly/s{size}",
              f"Samples/s{size}/{{mic}}/rr{{r}}/s{size}_{part}_vl{{v}}_rr{{r}}.flac", mics)

for pad, drum, part, mics, label in (("tom_high", "s13", "tom_clean", UNRULY_13, "13인치"),
                                     ("tom_mid", "s14", "tom_clean", UNRULY_14, "14인치"),
                                     ("tom_floor", "s22", "tom_clean_edge", UNRULY_22, "22인치 가장자리"),
                                     ("tom_low", "s22", "tom_clean", UNRULY_22, "22인치")):
    sound(pad, "unruly-toms", f"Unruly {label} (와이어를 푼 것)", "unruly-drums", "unruly/toms",
          f"Samples/{drum}/{{mic}}/rr{{r}}/{drum}_{part}_vl{{v}}_rr{{r}}.flac", mics)

sound("kick", "unruly-k20", "Unruly 20인치", "unruly-drums", "unruly/kick",
      "Samples/k20/{mic}/rr{r}/kick_clean_vl{v}_rr{r}.flac", UNRULY_KICK)
sound("kick", "unruly-k20-dirty", "Unruly 20인치 (와이어가 울림)", "unruly-drums", "unruly/kick-dirty",
      "Samples/k20/{mic}/rr{r}/kick_dirty_vl{v}_rr{r}.flac", UNRULY_KICK)

for pad, part in (("hihat_closed", "closed_tip"), ("hihat_half", "half_tip"), ("hihat_open", "open_tip"), ("hihat_pedal", "footchik")):
    sound(pad, "unruly-h14", "Unruly 14인치", "unruly-drums", "unruly/hihat",
          f"Samples/h14/{{mic}}/rr{{r}}/hh_{part}_vl{{v}}_rr{{r}}.flac", UNRULY_HAT)

for pad in ("crash1", "crash2"):
    sound(pad, "unruly-c16", "Unruly 16인치", "unruly-drums", "unruly/crash",
          "Samples/c16/{mic}/rr{r}/cr_edge_vl{v}_rr{r}.flac", UNRULY_CYMBAL)

for pad, part in (("ride", "bow"), ("ride_bell", "bell"), ("ride_edge", "edge")):
    sound(pad, "unruly-r20", "Unruly 20인치", "unruly-drums", "unruly/ride",
          f"Samples/r20/{{mic}}/rr{{r}}/ride_{part}_vl{{v}}_rr{{r}}.flac", UNRULY_CYMBAL)

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
# The Hat With The Phat: 20인치 라이드 두 장을 겹친 큰 하이햇
for pad, folder, part in (("hihat_closed", "closed", "closed_bow"), ("hihat_half", "loose", "loose_bow"),
                          ("hihat_open", "open", "open_bow"), ("hihat_pedal", "chik", "chik")):
    sound(pad, "phat-hat", "The Hat With The Phat 20인치", "hat-with-the-phat", "phat/hihat",
          f"Samples/{folder}/hat_20_{part}_vl{{v}}_rr{{r}}.wav", ONE_FILE)


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


def plan(spec: dict) -> list[dict]:
    """소리 하나에 쓸 타격 목록. 타격마다 섞을 마이크 파일들을 적는다."""
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

        if source["channels"] >= 2:
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
