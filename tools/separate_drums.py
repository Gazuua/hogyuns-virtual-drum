"""곡에서 드럼 소리만 분리한다 (Demucs).

리듬 게임의 채보를 만들 때 본 프로그램이 이 스크립트를 실행한다. 드럼만 남긴 소리에서 받아 적으면
완성곡에서 바로 받아 적는 것보다 훨씬 정확하다.

  python separate_drums.py --queue <폴더>            프로그램이 폴더에 넣어 주는 일을 하나씩 처리한다
  python separate_drums.py <입력.wav> <드럼.wav> [<반주.wav>]   파일 하나만 처리한다

일감 파일 <폴더>/<이름>.job 은 JSON이다: {"input": 경로, "drums": 경로, "backing": 경로(없어도 됨)}
  input   16비트 WAV (프로그램이 곡을 풀어서 써 준다)
  drums   드럼만 남긴 소리를 쓸 곳 (스테레오 16비트 WAV, 44.1 kHz)
  backing 드럼을 뺀 나머지를 쓸 곳 (스테레오 16비트 WAV, 44.1 kHz)
진행 정도는 <이름>.progress 에(0 ~ 1), 결과는 <이름>.done 에("ok" 또는 "error: ...") 쓴다.
폴더에 quit 파일이 생기거나 일이 5분 동안 없으면 끝낸다.

설치는 tools/setup-demucs.ps1 을 본다. 모델(htdemucs, MIT 라이선스)은 처음 쓸 때 한 번 내려받는다.
"""

import json
import os
import sys
import time
import wave

import numpy as np
import torch

# Demucs가 공개한 모델 파일은 예전 방식으로 저장돼 있어서, 새 PyTorch에서는 이 설정이 있어야 읽힌다.
os.environ.setdefault("TORCH_FORCE_NO_WEIGHTS_ONLY_LOAD", "1")

from demucs.apply import apply_model  # noqa: E402
from demucs.pretrained import get_model  # noqa: E402

MODEL_NAME = "htdemucs"
CHUNK_SECONDS = 40.0
OVERLAP_SECONDS = 1.0
IDLE_SECONDS = 300.0


def read_wav(path):
    """16비트 WAV를 (채널, 샘플) 모양의 float32 배열로 읽는다."""
    with wave.open(path, "rb") as reader:
        channels = reader.getnchannels()
        rate = reader.getframerate()

        if reader.getsampwidth() != 2:
            raise ValueError("16-bit WAV expected")

        data = np.frombuffer(reader.readframes(reader.getnframes()), dtype="<i2")

    audio = data.reshape(-1, channels).T.astype(np.float32) / 32768.0

    if channels == 1:
        audio = np.repeat(audio, 2, axis=0)

    return audio[:2], rate


def write_wav(path, audio, rate):
    """(채널, 샘플) 모양의 float 배열을 16비트 WAV로 쓴다. 임시 이름으로 쓴 뒤 바꿔서, 쓰다 만 파일이 보이지 않게 한다."""
    data = (np.clip(audio.T, -1.0, 1.0) * 32767.0).astype("<i2")
    temp = path + ".part"

    with wave.open(temp, "wb") as writer:
        writer.setnchannels(audio.shape[0])
        writer.setsampwidth(2)
        writer.setframerate(rate)
        writer.writeframes(data.tobytes())

    os.replace(temp, path)


def write_text(path, text):
    temp = path + ".part"

    with open(temp, "w", encoding="utf-8") as file:
        file.write(text)

    os.replace(temp, path)


class Separator:
    def __init__(self):
        self.device = "cuda" if torch.cuda.is_available() else "cpu"
        self.model = get_model(MODEL_NAME)
        self.model.to(self.device)
        self.model.eval()
        self.rate = self.model.samplerate
        self.drums = self.model.sources.index("drums")

    def separate(self, audio, rate, report=None):
        """(2, 샘플) 배열을 받아 (드럼, 나머지)를 돌려준다. 둘 다 (2, 샘플)이고 샘플레이트는 self.rate다."""
        mix = torch.from_numpy(np.ascontiguousarray(audio))

        if rate != self.rate:
            import julius

            mix = julius.resample_frac(mix, rate, self.rate)

        if self.device == "cuda":
            # 그래픽 카드에서는 곡 전체를 한 번에 넘긴다. 4분짜리 곡이 5초쯤이라 진행 정도를 따로 알릴 필요가 없고,
            # 토막으로 나눠 여러 번 부르는 것보다 훨씬 빠르다.
            reference = mix.mean(0)
            mean, deviation = reference.mean(), reference.std() + 1.0e-8

            with torch.no_grad():
                sources = apply_model(self.model, ((mix - mean) / deviation)[None], device=self.device,
                                      shifts=0, split=True, overlap=0.25, progress=False)[0]

            drums = sources[self.drums].cpu() * deviation

            if report is not None:
                report(1.0)

            return drums.numpy(), (mix - drums).numpy()

        total = mix.shape[1]
        chunk = int(CHUNK_SECONDS * self.rate)
        overlap = int(OVERLAP_SECONDS * self.rate)
        drums = torch.zeros_like(mix)
        weights = torch.zeros(total)

        # 긴 곡을 토막으로 나눠 처리하면서 진행 정도를 알린다. 토막이 겹치는 곳은 서서히 바꿔 잇는다.
        start = 0

        while start < total:
            end = min(total, start + chunk)
            piece = mix[:, start:end]

            # 모델은 평균 0, 표준편차 1로 맞춘 소리를 받는다.
            reference = piece.mean(0)
            mean, deviation = reference.mean(), reference.std() + 1.0e-8

            with torch.no_grad():
                sources = apply_model(self.model, ((piece - mean) / deviation)[None], device=self.device,
                                      shifts=0, split=True, overlap=0.25, progress=False)[0]

            part = sources[self.drums].cpu() * deviation

            window = torch.ones(end - start)
            ramp = min(overlap, end - start)

            if start > 0:
                window[:ramp] = torch.linspace(0.0, 1.0, ramp)

            if end < total:
                window[-ramp:] = torch.minimum(window[-ramp:], torch.linspace(1.0, 0.0, ramp))

            drums[:, start:end] += part * window
            weights[start:end] += window

            if report is not None:
                report(end / total)

            if end >= total:
                break

            start = end - overlap

        drums /= weights.clamp(min=1.0e-6)
        return drums.numpy(), (mix - drums).numpy()


def run_job(separator, job, report=None):
    audio, rate = read_wav(job["input"])
    drums, backing = separator.separate(audio, rate, report)
    write_wav(job["drums"], drums, separator.rate)

    if job.get("backing"):
        write_wav(job["backing"], backing, separator.rate)


def serve(folder):
    separator = Separator()
    write_text(os.path.join(folder, "ready"), separator.device)
    last_work = time.monotonic()

    while not os.path.exists(os.path.join(folder, "quit")):
        jobs = sorted(name for name in os.listdir(folder) if name.endswith(".job"))

        if not jobs:
            if time.monotonic() - last_work > IDLE_SECONDS:
                break

            time.sleep(0.2)
            continue

        name = jobs[0]
        base = os.path.join(folder, name[:-4])

        try:
            with open(os.path.join(folder, name), encoding="utf-8") as file:
                job = json.load(file)

            run_job(separator, job, lambda fraction: write_text(base + ".progress", f"{fraction:.3f}"))
            result = "ok"
        except Exception as error:  # noqa: BLE001 - 어떤 오류든 프로그램에 알려 주고 다음 일을 계속한다
            result = f"error: {error}"

        os.remove(os.path.join(folder, name))
        write_text(base + ".done", result)
        last_work = time.monotonic()


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--queue":
        serve(sys.argv[2])
    elif len(sys.argv) >= 3:
        started = time.monotonic()
        separator = Separator()
        loaded = time.monotonic()
        run_job(separator, {"input": sys.argv[1], "drums": sys.argv[2], "backing": sys.argv[3] if len(sys.argv) > 3 else None},
                lambda fraction: print(f"{fraction * 100:.0f}%", flush=True))
        print(f"device {separator.device}, model loaded in {loaded - started:.1f} s, separated in {time.monotonic() - loaded:.1f} s")
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
