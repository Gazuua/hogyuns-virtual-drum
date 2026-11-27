#!/usr/bin/env python3
"""기본 킷을 만든다.

Karoryfer Samples의 Big Rusty Drums(CC0 1.0)에서 필요한 타격만 내려받고,
마이크별로 나뉜 파일을 하나로 섞어 48kHz FLAC으로 저장한 뒤 kit.json을 쓴다.

    python -I tools/build_default_kit.py --repo <작업 폴더> --out kits/default

--repo 는 라이브러리를 받아 둘 폴더다(없으면 만든다). 전체 2.3GB를 받지 않고
필요한 파일만 받는다. git과 ffmpeg가 PATH에 있어야 한다.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))    # python -I는 스크립트 폴더를 찾지 않는다
from hvd_tools import ffmpeg_inputs, finish_graph, keep_backup, peak_db, pick_evenly, render_flac, run, write_kit  # noqa: E402

REPO_URL = "https://github.com/sfzinstruments/karoryfer.big-rusty-drums.git"
SOURCE_NAME = "Karoryfer Samples - Big Rusty Drums"
TARGET_PEAK_DB = -1.0

# 마이크 배율은 라이브러리 기본 프로그램(Programs/02-basic.sfz)의 기본값을 따른다.
SNARE_MICS = {"top": 0.31, "btm": 0.79, "oh": 0.47}
HIHAT_MICS = {"cl": 0.79, "oh": 0.79}
TOM_MICS = {"cl": 0.79, "oh": 0.55}
CYMBAL_MICS = {"cl": 0.31, "oh": 0.79}

# 가공한 패드가 킷에서 가장 큰 타격(킥)보다 커지지 않게 막는다. 그래야 전체 배율이 바뀌지 않는다.
LIMITER = "alimiter=limit=0.84:attack=1:release=80:level=0"

# id, 화면 이름, 기준 마이크의 SFZ 매핑 파일, 마이크 배율, 세기 레이어 수, 번갈아 재생 수,
# 최대 길이(초), 초크 그룹, 초크될 때 사라지는 시간(ms)
PADS = [
    dict(id="kick", name="킥", sfz="kick_24/k_kick.sfz", mics={"kick": 0.79, "oh": 0.30}, layers=6, rr=3),
    dict(id="snare", name="스네어", sfz="snare_14/sn_center_top.sfz", mics=SNARE_MICS, layers=6, rr=4),
    dict(id="snare_rim", name="스네어 림샷", sfz="snare_14/sn_rims_top.sfz", mics=SNARE_MICS, layers=4, rr=3),
    dict(id="sidestick", name="사이드스틱", sfz="snare_14/sn_ss_top.sfz", mics=SNARE_MICS, layers=3, rr=3),
    dict(id="hihat_closed", name="하이햇 닫힘", sfz="hihat_14/ht_tc_cl.sfz", mics=HIHAT_MICS, layers=5, rr=4,
         group="hihat", choke_ms=15),
    dict(id="hihat_half", name="하이햇 반열림", sfz="hihat_14/ht_ho_cl.sfz", mics=HIHAT_MICS, layers=3, rr=3,
         max_seconds=2.5, group="hihat", choke_ms=15),
    dict(id="hihat_open", name="하이햇 열림", sfz="hihat_14/ht_open_cl.sfz", mics=HIHAT_MICS, layers=4, rr=3,
         max_seconds=4.5, group="hihat", choke_ms=15),
    dict(id="hihat_pedal", name="하이햇 페달", sfz="hihat_14/ht_chik_cl.sfz", mics=HIHAT_MICS, layers=3, rr=3,
         group="hihat", choke_ms=15),
    dict(id="tom_high", name="탐 1", sfz="tom_14/tom_14_cl.sfz", mics=TOM_MICS, layers=4, rr=3),
    dict(id="tom_mid", name="탐 2", sfz="tom_15/tom_15_cl.sfz", mics=TOM_MICS, layers=4, rr=3),
    dict(id="tom_floor", name="플로어 탐", sfz="tom_18/tom_18_cl.sfz", mics=TOM_MICS, layers=4, rr=3),
    dict(id="tom_low", name="로우 탐", sfz="tom_22/tom_22_cl.sfz", mics=TOM_MICS, layers=4, rr=3),
    dict(id="crash1", name="크래시 1", sfz="crash_17/cr_cl.sfz", mics=CYMBAL_MICS, layers=4, rr=2,
         max_seconds=7.0, choke_ms=90),
    dict(id="crash2", name="크래시 2", sfz="crash_sizzle_17/crs_cr_cl.sfz", mics=CYMBAL_MICS, layers=4, rr=2,
         max_seconds=7.0, choke_ms=90),
    dict(id="china", name="차이나", sfz="china_18/cn_cl.sfz", mics=CYMBAL_MICS, layers=3, rr=2,
         max_seconds=5.0, choke_ms=90),
    # 크래시 후보: 같은 17인치 크래시를 다르게 가공하거나 다른 심벌과 겹친 것.
    dict(id="crash_bright", name="크래시 밝게", sfz="crash_17/cr_cl.sfz", mics={"cl": 0.6, "oh": 0.8},
         layers=3, rr=2, max_seconds=7.0, choke_ms=90,
         fx="highshelf=f=5000:g=6:t=q:w=0.7,acompressor=threshold=0.09:ratio=3:attack=1:release=300:makeup=2.5,"
            + LIMITER),
    dict(id="crash_trash", name="크래시+차이나", sfz="crash_17/cr_cl.sfz", mics=CYMBAL_MICS,
         layers=3, rr=2, max_seconds=7.0, choke_ms=90,
         layer_with=[dict(sfz="china_18/cn_cl.sfz", mics=CYMBAL_MICS, gain=0.85)],
         fx="highshelf=f=6000:g=3:t=q:w=0.7,acompressor=threshold=0.125:ratio=2.5:attack=2:release=250:makeup=1.8,"
            + LIMITER),
    dict(id="crash_stack", name="크래시+스택", sfz="crash_17/cr_cl.sfz", mics={"cl": 0.5, "oh": 0.8},
         layers=3, rr=2, max_seconds=7.0, choke_ms=90,
         layer_with=[dict(sfz="stack_3_layer/st_ed_cl.sfz", mics=CYMBAL_MICS, gain=1.0)],
         fx="highshelf=f=5500:g=4:t=q:w=0.7,acompressor=threshold=0.125:ratio=2.5:attack=2:release=250:makeup=1.8,"
            + LIMITER),
    dict(id="ride", name="라이드", sfz="ride_22/rd_cl.sfz", mics=CYMBAL_MICS, layers=5, rr=2,
         max_seconds=6.0, choke_ms=90),
    dict(id="ride_bell", name="라이드 벨", sfz="ride_22/rd_bl_cl.sfz", mics=CYMBAL_MICS, layers=4, rr=2,
         max_seconds=6.0, choke_ms=90),
    dict(id="ride_edge", name="라이드 엣지", sfz="ride_22/rd_ed_cl.sfz", mics=CYMBAL_MICS, layers=3, rr=2,
         max_seconds=7.0, choke_ms=90),
]


def git(repo: Path, *args: str, **kwargs) -> subprocess.CompletedProcess:
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0")
    return run(["git", "-C", str(repo), *args], env=env, **kwargs)


def ensure_repo(repo: Path) -> None:
    if (repo / ".git").exists():
        return

    repo.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0")
    run(["git", "clone", "--filter=blob:none", "--no-checkout", "--depth", "1", REPO_URL, str(repo)], env=env)


def list_tree(repo: Path) -> dict[str, str]:
    """저장소 안 경로 → blob id."""
    output = git(repo, "ls-tree", "-r", "HEAD", capture_output=True).stdout
    tree = {}

    for line in output.splitlines():
        meta, path = line.split("\t", 1)
        tree[path] = meta.split()[2]

    return tree


def fetch_and_checkout(repo: Path, tree: dict[str, str], paths: list[str]) -> None:
    """필요한 파일을 한 번의 요청으로 받고 작업 폴더에 꺼낸다."""
    missing = [p for p in paths if not (repo / p).exists()]

    if not missing:
        return

    oids = sorted({tree[p] for p in missing})

    # 한 번에 너무 많이 요청하면 서버가 끊는 경우가 있어 나눠 받고, 실패하면 몇 번 다시 시도한다.
    command = ["git", "-C", str(repo), "-c", "fetch.negotiationAlgorithm=noop", "fetch", "origin", "--no-tags",
               "--no-write-fetch-head", "--recurse-submodules=no", "--filter=blob:none", "--stdin"]
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0")

    for start in range(0, len(oids), 100):
        # 목록은 바이트로 넘긴다. 텍스트 모드로 넘기면 Windows에서 줄 끝에 \r 이 붙어 git이 읽지 못한다.
        chunk = ("\n".join(oids[start:start + 100]) + "\n").encode("ascii")

        for attempt in range(1, 5):
            try:
                subprocess.run(command, input=chunk, capture_output=True, check=True, env=env)
                break
            except subprocess.CalledProcessError as error:
                message = error.stderr.decode("utf-8", errors="replace").strip()[-300:]
                print(f"  받기 실패 ({attempt}번째): {message}", file=sys.stderr)

                if attempt == 4:
                    raise

                time.sleep(3 * attempt)

    for start in range(0, len(missing), 150):
        git(repo, "checkout", "HEAD", "--", *missing[start:start + 150], capture_output=True)


def parse_mapping(sfz_path: Path) -> list[dict]:
    """마이크 하나의 SFZ 매핑 파일에서 세기 구간과 샘플 경로를 읽는다."""
    groups: list[dict] = []
    region = None

    for raw in sfz_path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.split("//", 1)[0].strip()

        if not line:
            continue

        if line.startswith("<group>"):
            groups.append({"lovel": 1, "hivel": 127, "regions": []})
            region = None
            continue

        if line.startswith("<region>"):
            if not groups:
                groups.append({"lovel": 1, "hivel": 127, "regions": []})

            region = {"seq": 1, "sample": None}
            groups[-1]["regions"].append(region)
            continue

        match = re.match(r"(\w+)=(.+)", line)

        if not match or not groups:
            continue

        key, value = match.group(1), match.group(2).strip()

        if key == "sample" and region is not None:
            region["sample"] = value.replace("\\", "/").removeprefix("../")
        elif key == "seq_position" and region is not None:
            region["seq"] = int(value)
        elif key in ("lovel", "hivel") and region is None:
            groups[-1][key] = int(value)

    groups = [g for g in groups if any(r["sample"] for r in g["regions"])]
    groups.sort(key=lambda g: g["hivel"])
    return groups


def plan_pad(repo: Path, tree: dict[str, str], pad: dict) -> list[dict]:
    """패드 하나에 쓸 타격 목록을 만든다. 타격마다 마이크별 원본 파일을 적는다."""
    groups = parse_mapping(repo / "Programs" / "mappings" / pad["sfz"])

    if not groups:
        raise RuntimeError(f"no velocity groups in {pad['sfz']}")

    chosen = [groups[i] for i in pick_evenly(len(groups), pad["layers"])]
    hits = []

    # 겹칠 심벌도 같은 개수의 세기 단계를 고른다.
    extras = []

    for extra in pad.get("layer_with", []):
        extra_groups = parse_mapping(repo / "Programs" / "mappings" / extra["sfz"])
        extras.append((extra, [extra_groups[i] for i in pick_evenly(len(extra_groups), len(chosen))]))

    for layer_number, group in enumerate(chosen, start=1):
        is_top = group is chosen[-1]
        up_to = 1.0 if is_top else round(group["hivel"] / 127.0, 4)
        regions = sorted((r for r in group["regions"] if r["sample"]), key=lambda r: r["seq"])[:pad["rr"]]

        for rr_number, region in enumerate(regions, start=1):
            reference = region["sample"]
            parts = reference.split("/")
            sources = []

            for mic, gain in pad["mics"].items():
                candidate = "/".join(parts[:-2] + [mic, parts[-1]])

                if gain > 0 and candidate in tree:
                    sources.append({"path": candidate, "gain": gain, "stereo": mic == "oh"})

            for extra, extra_chosen in extras:
                extra_group = extra_chosen[min(layer_number - 1, len(extra_chosen) - 1)]
                extra_regions = sorted((r for r in extra_group["regions"] if r["sample"]), key=lambda r: r["seq"])
                extra_parts = extra_regions[(rr_number - 1) % len(extra_regions)]["sample"].split("/")

                for mic, gain in extra["mics"].items():
                    candidate = "/".join(extra_parts[:-2] + [mic, extra_parts[-1]])

                    if gain > 0 and candidate in tree:
                        sources.append({"path": candidate, "gain": gain * extra.get("gain", 1.0),
                                        "stereo": mic == "oh"})

            if not sources:
                raise RuntimeError(f"no mic files for {reference}")

            hits.append({
                "pad": pad["id"],
                "layer": layer_number,
                "up_to": up_to,
                "output": f"samples/{pad['id']}/{pad['id']}_v{layer_number:02d}_r{rr_number}.flac",
                "sources": sources,
                "max_seconds": pad.get("max_seconds"),
                "fx": pad.get("fx"),
            })

    return hits


def filter_graph(hit: dict, tail: str) -> str:
    chains = []
    labels = []

    for index, source in enumerate(hit["sources"]):
        upmix = "" if source["stereo"] else "pan=stereo|c0=c0|c1=c0,"
        chains.append(f"[{index}:a]{upmix}volume={source['gain']}[m{index}]")
        labels.append(f"[m{index}]")

    return finish_graph(chains, labels, [hit["fx"]] if hit.get("fx") else [], hit["max_seconds"], tail)


def inputs_of(repo: Path, hit: dict) -> list[str]:
    return ffmpeg_inputs(repo / source["path"] for source in hit["sources"])


def measure_peak(repo: Path, hit: dict) -> float:
    return peak_db([*inputs_of(repo, hit), "-filter_complex", filter_graph(hit, "volumedetect"), "-map", "[out]"],
                   hit["output"])


def render_hit(repo: Path, out_dir: Path, hit: dict, scale: float) -> None:
    render_flac(inputs_of(repo, hit), filter_graph(hit, f"volume={scale:.6f}"), out_dir / hit["output"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", required=True, type=Path, help="라이브러리를 받아 둘 폴더")
    parser.add_argument("--out", required=True, type=Path, help="킷을 만들 폴더 (예: kits/default)")
    parser.add_argument("--only", default="", help="쉼표로 나눈 패드 id. 이 패드의 샘플만 다시 만든다")
    options = parser.parse_args()
    only = {name.strip() for name in options.only.split(",") if name.strip()}

    repo, out_dir = options.repo.resolve(), options.out.resolve()

    ensure_repo(repo)
    tree = list_tree(repo)

    print("SFZ 매핑과 라이선스 받는 중...")
    program_paths = [p for p in tree if p.startswith("Programs/") or p == "LICENSE"]
    fetch_and_checkout(repo, tree, program_paths)

    license_text = (repo / "LICENSE").read_text(encoding="utf-8", errors="replace")

    if "CC0 1.0 Universal" not in license_text:
        print("라이선스 파일이 CC0 1.0이 아닙니다. 중단합니다.", file=sys.stderr)
        return 1

    hits = [hit for pad in PADS for hit in plan_pad(repo, tree, pad)]
    workers = max(2, (os.cpu_count() or 4) // 2)
    kit_path = out_dir / "kit.json"

    if only:
        unknown = only - {pad["id"] for pad in PADS}

        if unknown:
            print(f"모르는 패드: {', '.join(sorted(unknown))}", file=sys.stderr)
            return 1

        # 전체 배율은 지난번에 킷 전체를 만들 때 정한 값을 그대로 쓴다.
        scale = json.loads(kit_path.read_text(encoding="utf-8"))["build"]["scale"]
        to_render = [hit for hit in hits if hit["pad"] in only]
        missing = [hit["output"] for hit in hits
                   if hit["pad"] not in only and not (out_dir / hit["output"]).exists()]

        if missing:
            print(f"다시 만들지 않는 패드의 샘플이 없습니다: {missing[0]} 등 {len(missing)}개", file=sys.stderr)
            return 1
    else:
        to_render = hits

    sources = sorted({source["path"] for hit in to_render for source in hit["sources"]})
    print(f"타격 {len(to_render)}개, 원본 파일 {len(sources)}개 받는 중...")
    fetch_and_checkout(repo, tree, sources)

    if not only:
        # 가공하지 않은 타격 가운데 가장 큰 것을 기준으로 킷 전체의 배율을 정한다.
        print("음량 측정 중...")
        with ThreadPoolExecutor(max_workers=workers) as pool:
            peaks = list(pool.map(lambda hit: measure_peak(repo, hit), hits))

        loudest = max(peaks)
        scale = 10 ** ((TARGET_PEAK_DB - loudest) / 20.0)
        print(f"가장 큰 타격 {loudest:.1f} dB, 전체 배율 {scale:.3f}")

    print("마이크를 섞어 저장하는 중...")
    with ThreadPoolExecutor(max_workers=workers) as pool:
        list(pool.map(lambda hit: render_hit(repo, out_dir, hit, scale), to_render))

    commit = git(repo, "rev-parse", "HEAD", capture_output=True).stdout.strip()
    pads_json = []

    # 사용자가 화면이나 kit.json에서 손본 값(이름, 음량, 팬, 피치, 세기 곡선, 리버브 양, 시작 지점,
    # MIDI 노트, 그림 위의 자리)과 킷 전체의 이펙트 설정은 다시 만들어도 유지한다.
    previous = {}
    previous_pads = {}

    if kit_path.exists():
        previous = json.loads(kit_path.read_text(encoding="utf-8"))
        previous_pads = {pad["id"]: pad for pad in previous.get("pads", [])}

    for pad in PADS:
        layers: dict[int, dict] = {}

        for hit in hits:
            if hit["pad"] == pad["id"]:
                layer = layers.setdefault(hit["layer"], {"upTo": hit["up_to"], "samples": []})
                layer["samples"].append(hit["output"])

        entry = {"id": pad["id"], "name": pad["name"], "volume": 1.0, "pan": 0.0, "pitch": 0.0}

        for key in ("name", "volume", "pan", "pitch", "velocityCurve", "reverbSend", "startMs", "midiNote", "view", "fixedView"):
            if key in previous_pads.get(pad["id"], {}):
                entry[key] = previous_pads[pad["id"]][key]

        if "group" in pad:
            entry["group"] = pad["group"]

        if "choke_ms" in pad:
            entry["chokeFadeMs"] = pad["choke_ms"]

        entry["layers"] = [layers[number] for number in sorted(layers)]

        # 모아 둔 소리(tools/build_sounds.py 가 넣은 "sounds")와 그 가운데 고른 것은 그대로 둔다.
        # 여기서 다시 만든 샘플이 목록의 "원래 소리"가 된다.
        previous_pad = previous_pads.get(pad["id"], {})

        if "sounds" in previous_pad:
            sounds = previous_pad["sounds"]

            for sound in sounds:
                if sound.get("id") == "original":
                    sound["layers"] = json.loads(json.dumps(entry["layers"]))

            entry["sounds"] = sounds
            chosen = next((sound for sound in sounds if sound.get("id") == previous_pad.get("sound")), None)

            if chosen is not None:
                entry["sound"] = chosen["id"]
                entry["layers"] = json.loads(json.dumps(chosen["layers"]))

        pads_json.append(entry)

    # 여기서 만들지 않는 패드(나중에 더한 하이햇 스플래시, 스네어 울림 등)는 그대로 둔다.
    made_ids = {pad["id"] for pad in PADS}
    pads_json += [pad for pad in previous.get("pads", []) if pad["id"] not in made_ids]

    kit = {
        "name": "Big Rusty (기본 킷)",
        "source": f"{SOURCE_NAME} ({REPO_URL} @ {commit[:12]})",
        "license": "CC0 등",
        "build": {"scale": round(scale, 6)},
        # 칠 때마다 음량과 음높이를 조금씩 흔든다. 사용자가 고친 값이 있으면 그 값을 유지한다.
        "humanize": previous.get("humanize", {"volume": 0.04, "pitchCents": 3}),
        "pads": pads_json,
    }

    if "fx" in previous:
        kit["fx"] = previous["fx"]

    out_dir.mkdir(parents=True, exist_ok=True)
    keep_backup(kit_path)
    write_kit(kit_path, kit)
    (out_dir / "CREDITS.txt").write_text(
        f"이 킷의 샘플은 {SOURCE_NAME}에서 가져왔습니다.\n"
        f"출처: {REPO_URL} (커밋 {commit})\n"
        "라이선스: CC0 1.0 Universal (퍼블릭 도메인 기증)\n\n"
        "tools/build_default_kit.py 가 마이크별 파일을 섞고 48kHz로 변환해 만들었습니다.\n",
        encoding="utf-8")

    total_bytes = sum((out_dir / hit["output"]).stat().st_size for hit in hits)
    print(f"완료: 패드 {len(PADS)}개, 샘플 {len(hits)}개(이번에 만든 것 {len(to_render)}개), "
          f"{total_bytes / 1e6:.1f} MB → {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
