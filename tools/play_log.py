"""연주 로그(logs/play-*.jsonl)를 읽어, 곡에 맞춰 친 것을 정리해 보여 준다.

    python -I tools/play_log.py                      # 가장 최근 로그의 가장 긴 연주
    python -I tools/play_log.py --list               # 로그에 든 연주(곡을 틀고 친 구간)들
    python -I tools/play_log.py --take 2             # --list의 번호로 고르기
    python -I tools/play_log.py --log logs/play-20261125-222109.jsonl
    python -I tools/play_log.py --no-score           # 글 악보는 빼고 요약만

연주 하나 = 곡 하나를 틀어 둔 동안(track 줄부터 다음 track 줄까지) 곡 위치가 붙은 타격들.
그 곡의 채보(charts/)가 있으면 채보의 박 격자 위에 친 것(U)과 채보(C)를 나란히 적고, 얼마나 겹치는지 센다.
로그의 song과 채보의 시각은 같은 시간축(프로그램이 곡을 푼 것)이라 그대로 견줄 수 있다.
"""
import argparse
import bisect
import json
import statistics
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# 글 악보의 줄과 이름은 src/app/ChartTools.cpp의 chartToTab과 같다.
ROWS = ["crash", "ride", "hihat", "snare", "tom_high", "tom_mid", "tom_floor", "kick"]
SHORT = ["CR", "RD", "HH", "SN", "T1", "T2", "T3", "KK"]
GROUPS = {"kick": "킥", "snare": "스네어", "hihat": "하이햇+라이드", "ride": "하이햇+라이드", "crash": "크래시",
          "tom_high": "탐", "tom_mid": "탐", "tom_floor": "탐"}


def lane_of(pad: str):
    """패드 id에서 레인을 짐작한다 (src/core/Chart.cpp의 laneForPadId와 같은 규칙)."""
    pad = pad.lower()

    if pad.startswith(("kick", "bass", "bd")):
        return "kick"
    if pad.startswith(("snare", "sidestick", "rim", "clap", "sd")):
        return "snare"
    if pad.startswith(("hihat", "hat", "hh")):
        return "hihat"
    if pad.startswith("ride"):
        return "ride"
    if pad.startswith(("crash", "china", "splash", "cymbal")):
        return "crash"
    if pad.startswith("tom"):
        if any(word in pad for word in ("floor", "low", "3", "4")):
            return "tom_floor"
        return "tom_mid" if ("mid" in pad or "2" in pad) else "tom_high"

    return None


def read_takes(path: Path):
    """로그를 연주들로 나눈다. 곡을 새로 열 때만 나눈다.

    연습 기록 화면(src/app/PracticeLog.cpp)은 되감기와 게임 시작에서도 나누고 20타 미만을 버린다. 그래서 연주 수가 다를 수 있다.
    """
    takes = []
    current = None
    game = None

    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw.strip():
            continue

        line = json.loads(raw)
        event = line["e"]

        if event == "track":
            current = {"file": line.get("file", ""), "length": line.get("length", 0.0), "hits": [], "speeds": set(),
                       "game": None} if line.get("file") else None

            if current:
                takes.append(current)
        elif event == "game" and line.get("state") == "start":
            game = line

            if current and line.get("audio") == current["file"]:
                current["game"] = line
        elif event in ("play", "speed") and current:
            current["speeds"].add(line.get("speed", 1.0))
        elif event == "hit" and current and "song" in line:
            if game and current["game"] is None and game.get("audio") == current["file"]:
                current["game"] = game

            current["hits"].append(line)

    return [take for take in takes if take["hits"]]


def find_chart(take):
    """연주한 곡의 채보를 찾는다. 리듬 게임에서 드럼 뺀 반주로 쳤으면 game 줄의 원래 곡으로 찾는다."""
    index_path = ROOT / "charts" / "index.json"

    if not index_path.exists():
        return None

    song = (take["game"] or {}).get("song") or take["file"]
    index = json.loads(index_path.read_text(encoding="utf-8"))

    for key, entry in index.items():
        if entry.get("file") == song or Path(take["file"]).name == f"{key}.backing.ogg":
            chart_path = ROOT / "charts" / f"{key}.json"

            if chart_path.exists():
                return json.loads(chart_path.read_text(encoding="utf-8"))

    return None


def beat_position(beats, seconds):
    """박 번호 + 박 안의 위치. 격자 밖은 가장 가까운 박의 길이로 이어서 센다 (src/core/PracticeStats.cpp의 beatPositionOf와 같다)."""
    index = bisect.bisect_right(beats, seconds) - 1

    if index < 0:
        return (seconds - beats[0]) / (beats[1] - beats[0])

    if index >= len(beats) - 1:
        return len(beats) - 1 + (seconds - beats[-1]) / (beats[-1] - beats[-2])

    return index + (seconds - beats[index]) / (beats[index + 1] - beats[index])


def match(mine, theirs, window=0.08):
    """시각의 두 목록에서 window 안에 드는 것끼리 하나씩 짝짓는다. (짝의 수, 짝마다 theirs - mine)"""
    used = [False] * len(theirs)
    offsets = []
    start = 0

    for t in mine:
        while start < len(theirs) and theirs[start] < t - window:
            start += 1

        best = None

        for index in range(start, len(theirs)):
            if theirs[index] > t + window:
                break

            if not used[index] and (best is None or abs(theirs[index] - t) < abs(theirs[best] - t)):
                best = index

        if best is not None:
            used[best] = True
            offsets.append(theirs[best] - t)

    return offsets


def place(notes, beats, first):
    """(시각, 레인, 세기)들을 마디 -> 레인 -> 16칸 글자로 놓는다."""
    bars = {}

    for seconds, lane, velocity in notes:
        position = beat_position(beats, seconds) - first

        if position < -0.13:
            continue

        slot = round(position * 4)
        bar, cell = divmod(slot, 16)
        mark = "x" if velocity >= 0.45 else "o"

        if abs(position * 4 - slot) > 0.3:
            mark = "~"

        bars.setdefault(bar, {}).setdefault(lane, ["."] * 16)[cell] = mark

    return bars


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--log", help="로그 파일. 주지 않으면 logs/의 가장 최근 것")
    parser.add_argument("--list", action="store_true", help="로그에 든 연주들만 보여 준다")
    parser.add_argument("--take", type=int, help="--list의 번호. 주지 않으면 타격이 가장 많은 연주")
    parser.add_argument("--no-score", action="store_true", help="글 악보를 빼고 요약만")
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")

    logs = sorted((ROOT / "logs").glob("play-*.jsonl"))
    path = Path(args.log) if args.log else (logs[-1] if logs else None)

    if path is None or not path.exists():
        raise SystemExit("로그가 없습니다 (logs/play-*.jsonl)")

    takes = read_takes(path)
    print(f"로그: {path.name}, 곡에 맞춰 친 연주 {len(takes)}개")

    for number, take in enumerate(takes, 1):
        songs = [hit["song"] for hit in take["hits"]]
        print(f"  {number}. {Path(take['file']).name}: 타격 {len(take['hits'])}개, 곡의 {min(songs):.1f} ~ {max(songs):.1f}초"
              + (", 리듬 게임" if take["game"] else ""))

    if args.list or not takes:
        return

    take = takes[args.take - 1] if args.take else max(takes, key=lambda item: len(item["hits"]))
    hits = take["hits"]
    songs = [hit["song"] for hit in hits]
    span = max(songs) - min(songs)

    print(f"\n[{Path(take['file']).name}] 곡 길이 {take['length']:.0f}초, 친 구간 {min(songs):.1f} ~ {max(songs):.1f}초, "
          f"타격 {len(hits)}개 (초당 {len(hits) / max(span, 1.0):.1f}개)")

    if take["speeds"] - {1.0}:
        print("  재생 속도:", ", ".join(f"{speed:.2f}" for speed in sorted(take["speeds"])))

    print("  친 방법:", ", ".join(f"{name} {count}" for name, count in Counter(hit["src"] for hit in hits).most_common()))
    print("  패드:", ", ".join(f"{name} {count}" for name, count in Counter(hit["pad"] for hit in hits).most_common()))
    keys = Counter(hit["key"] for hit in hits if "key" in hit)

    if keys:
        print("  키:", ", ".join(f"{name} {count}" for name, count in keys.most_common()))

    played = [(hit["song"], lane_of(hit["pad"]), hit["v"]) for hit in hits if lane_of(hit["pad"]) and hit["src"] not in ("auto", "replay")]
    chart = find_chart(take)

    if chart is None or len(chart.get("beats", [])) < 8:
        print("  이 곡의 채보가 없어서 박에 맞춰 보지 못했습니다.")
        return

    beats, first = chart["beats"], chart["firstDownbeat"]
    begin, end = min(songs) - 0.5, max(songs) + 0.5
    charted = [(note[0], note[1], note[2]) for note in chart["notes"] if begin <= note[0] <= end]
    print(f"\n채보(받아 적은 전부): 템포 {chart['tempo']:.1f}, 같은 구간의 노트 {len(charted)}개 (초당 {len(charted) / max(span, 1.0):.1f}개)")

    # 박 격자(16분음표)에서 얼마나 벗어났는지
    deviations = []

    for seconds, _, _ in played:
        position = beat_position(beats, seconds) * 4.0
        index = max(0, min(len(beats) - 2, int(beat_position(beats, seconds))))
        deviations.append((position - round(position)) / 4.0 * (beats[index + 1] - beats[index]) * 1000.0)

    if deviations:
        absolute = sorted(abs(value) for value in deviations)
        print("채보의 16분음표 격자와의 차이: 가운뎃값 %+.0f ms (음수 = 빠름), 절댓값의 가운뎃값 %.0f ms, 30 ms 안 %d%%" % (
            statistics.median(deviations), absolute[len(absolute) // 2],
            100 * sum(1 for value in absolute if value <= 30.0) // len(absolute)))

    print("채보와 겹치는 정도 (같은 악기, 80 ms 안):")

    for label in ("킥", "스네어", "하이햇+라이드", "크래시", "탐"):
        mine = sorted(t for t, lane, _ in played if GROUPS[lane] == label)
        theirs = sorted(t for t, lane, _ in charted if GROUPS[lane] == label)
        offsets = match(mine, theirs)
        offset = statistics.median(offsets) * 1000.0 if offsets else 0.0
        print("  %-8s 친 것 %4d, 채보 %4d, 같은 자리 %4d  (친 것의 %3.0f%%, 채보의 %3.0f%%, 채보가 %+.0f ms)" % (
            label, len(mine), len(theirs), len(offsets), 100.0 * len(offsets) / max(1, len(mine)),
            100.0 * len(offsets) / max(1, len(theirs)), offset))

    if args.no_score:
        return

    mine_bars = place(played, beats, first)
    their_bars = place(charted, beats, first)
    last = max(list(mine_bars) + list(their_bars))
    print("\n글 악보 (한 칸 = 16분음표, U = 친 것, C = 채보, x 보통 / o 여리게 / ~ 격자에서 벗어남)")

    def row(bars, bar, lane):
        cells = bars.get(bar, {}).get(lane)
        text = "".join(cells) if cells else "." * 16
        return " ".join(text[i:i + 4] for i in range(0, 16, 4))

    for start in range(0, last + 1, 4):
        seconds = beats[min(len(beats) - 1, max(0, first + start * 4))]
        print(f"\n마디 {start + 1}  ({seconds:.1f}초)")

        for lane, name in zip(ROWS, SHORT):
            for mark, bars in (("U", mine_bars), ("C", their_bars)):
                if any(lane in bars.get(bar, {}) for bar in range(start, start + 4)):
                    print(f"{mark} {name} |" + "|".join(row(bars, bar, lane) for bar in range(start, start + 4)) + "|")


if __name__ == "__main__":
    main()
