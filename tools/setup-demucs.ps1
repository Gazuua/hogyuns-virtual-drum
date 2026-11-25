# 드럼 분리 도구(Demucs)를 tools/demucs-env 에 설치한다. 리듬 게임의 채보를 정밀하게 만들 때 쓴다.
#
#   pwsh tools/setup-demucs.ps1          # 설치. NVIDIA 그래픽 카드가 있으면 GPU용(약 4.4GB), 없으면 CPU용(약 1.3GB)
#   pwsh tools/setup-demucs.ps1 -Cpu     # 그래픽 카드가 있어도 CPU용으로 설치
#   pwsh tools/setup-demucs.ps1 -Remove  # 지우기
#
# 이 PC 전체가 아니라 이 폴더 안의 가상환경에만 설치한다. 폴더를 지우면 원래대로 돌아간다.
# 분리 모델(htdemucs)은 처음 쓸 때 한 번 내려받아 사용자 폴더의 .cache\torch 에 둔다 (약 80MB).
# 설치돼 있지 않으면 프로그램은 완성곡에서 바로 받아 적는다 (덜 정확하다).
#
# 4분짜리 곡 하나를 분리하는 데 GPU(RTX 5060)로 약 6초, CPU(Ryzen 9 9950X)로 약 30초 걸린다.
#
# Python 3.12가 필요하다. 설치하는 PyTorch 버전:
# - GPU용은 2.8.0 (CUDA 12.8). RTX 50 시리즈는 2.7 이상이 필요하다.
# - CPU용은 2.5.1.
param(
    [switch]$Cpu,
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'
$envDir = Join-Path $PSScriptRoot 'demucs-env'

if ($Remove) {
    if (Test-Path $envDir) { Remove-Item -Recurse -Force $envDir }
    "Removed $envDir"
    exit 0
}

if (-not (Test-Path (Join-Path $envDir 'Scripts\python.exe'))) {
    py -3.12 -m venv $envDir
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.12 is required (py -3.12).' }
}

$python = Join-Path $envDir 'Scripts\python.exe'
$hasNvidia = [bool](Get-Command nvidia-smi -ErrorAction SilentlyContinue)

if ($hasNvidia -and -not $Cpu) {
    & $python -m pip install --disable-pip-version-check torch==2.8.0 torchaudio==2.8.0 --index-url https://download.pytorch.org/whl/cu128
}
else {
    & $python -m pip install --disable-pip-version-check torch==2.5.1 torchaudio==2.5.1 --index-url https://download.pytorch.org/whl/cpu
}

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $python -m pip install --disable-pip-version-check demucs numpy soundfile
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $python -c "import torch, demucs; print('torch', torch.__version__, '/ demucs', demucs.__version__, '/ GPU', torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'not used')"
exit $LASTEXITCODE
