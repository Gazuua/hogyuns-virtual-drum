"""킷을 만드는 도구(build_default_kit.py, build_sounds.py)가 함께 쓰는 것.

`python -I`로 돌리면 스크립트 폴더가 sys.path에 없어서, 각 도구가 이 폴더를 sys.path 앞에 넣고 읽는다.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
from collections.abc import Iterable
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SAMPLE_RATE = 48000
FADE_OUT_SECONDS = 1.5


def run(args: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(args, check=True, text=True, encoding="utf-8", errors="replace", **kwargs)


def pick_evenly(count: int, wanted: int) -> list[int]:
    """0 ~ count-1에서 wanted개를 고르게 고른다. 끝(count-1)은 늘 든다."""
    if wanted >= count:
        return list(range(count))

    if wanted == 1:
        return [count - 1]

    return sorted({round(i * (count - 1) / (wanted - 1)) for i in range(wanted)})


# ---------------------------------------------------------------------------------------------------
# ffmpeg

def ffmpeg_inputs(paths: Iterable) -> list[str]:
    return [arg for path in paths for arg in ("-i", str(path))]


def finish_graph(chains: list[str], labels: list[str], effects: list[str], max_seconds, tail: str) -> str:
    """마이크별 사슬(chains, 끝 이름은 labels)을 섞고 effects → 48kHz → 길이 자르기 → tail 순으로 이어
    ffmpeg 필터 그래프를 만든다. 결과는 [out]."""
    chains = list(chains)

    if len(labels) > 1:
        chains.append(f"{''.join(labels)}amix=inputs={len(labels)}:normalize=0:duration=longest[mix]")
        current = "[mix]"
    else:
        current = labels[0]

    post = [*effects, f"aresample={SAMPLE_RATE}:resampler=soxr:precision=28"]

    if max_seconds:
        post.append(f"atrim=end={max_seconds}")
        post.append(f"afade=t=out:st={max_seconds - FADE_OUT_SECONDS}:d={FADE_OUT_SECONDS}")

    post.append(tail)
    chains.append(f"{current}{','.join(post)}[out]")
    return ";".join(chains)


def peak_db(args: list[str], what: str = "a peak") -> float:
    """입력과 필터(args, 끝에 volumedetect)로 잰 최댓값(dB)."""
    result = run(["ffmpeg", "-hide_banner", "-nostdin", *args, "-f", "null", "-"], capture_output=True)
    match = re.search(r"max_volume:\s*(-?[\d.]+) dB", result.stderr)

    if not match:
        raise RuntimeError(f"could not measure {what}")

    return float(match.group(1))


def render_flac(inputs: list[str], graph: str, target: Path) -> None:
    """필터 그래프의 [out]을 24비트 FLAC으로 쓴다."""
    target.parent.mkdir(parents=True, exist_ok=True)
    run(["ffmpeg", "-hide_banner", "-nostdin", "-v", "error", "-y", *inputs,
         "-filter_complex", graph, "-map", "[out]",
         "-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24", str(target)],
        capture_output=True)


# ---------------------------------------------------------------------------------------------------
# kit.json

def keep_backup(path: Path) -> None:
    """kit.json을 다시 쓰기 전에 지금 내용을 backups/에 남긴다.

    프로그램이 남기는 백업과 같은 자리, 같은 이름 규칙이다 (src/app/Backups.h). 그래서 킷 화면의 "되돌리기…"에 함께 보인다.
    """
    if not path.exists():
        return

    try:
        name = "__".join(path.resolve().relative_to(ROOT).parts)
    except ValueError:
        name = path.name

    folder = ROOT / "backups" / name
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / (datetime.now().strftime("%Y%m%d-%H%M%S") + path.suffix)

    if not target.exists():
        shutil.copy2(path, target)


def write_kit(path: Path, kit: dict) -> None:
    """kit.json을 LF 줄바꿈, 들여쓰기 2로 쓴다. 임시 파일에 다 쓴 뒤 바꿔치기해서 쓰다 만 파일이 남지 않게 한다."""
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(kit, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    temporary.replace(path)
