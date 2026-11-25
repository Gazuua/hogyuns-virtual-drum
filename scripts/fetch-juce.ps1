# JUCE를 고정 버전으로 external/JUCE 에 내려받는다. 이미 있으면 아무것도 하지 않는다.
param(
    [string]$Tag = '9.0.6'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$target = Join-Path $root 'external\JUCE'

if (Test-Path (Join-Path $target 'CMakeLists.txt')) {
    "JUCE already present: $target"
    exit 0
}

New-Item -ItemType Directory -Force (Join-Path $root 'external') | Out-Null
git clone --depth 1 --branch $Tag https://github.com/juce-framework/JUCE.git $target
exit $LASTEXITCODE
