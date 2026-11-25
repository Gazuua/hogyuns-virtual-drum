# 빌드 스크립트: Visual Studio 개발 환경을 불러온 뒤 CMake + Ninja로 빌드한다.
#
#   pwsh scripts/build.ps1                  # Release 빌드
#   pwsh scripts/build.ps1 -Config Debug    # Debug 빌드
#   pwsh scripts/build.ps1 -Run             # 빌드 후 실행
#   pwsh scripts/build.ps1 -Clean           # 빌드 폴더를 지우고 다시 빌드
#   pwsh scripts/build.ps1 -ProductName HogyunsVirtualDrumDev
#                                           # 프로그램이 켜져 있을 때: 다른 이름의 실행 파일로 빌드
#
# JUCE의 보조 도구(juceaide)는 쓰지 않는다. CMakeLists.txt가 JUCE 모듈을 직접 컴파일한다.
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Config = 'Release',
    [string]$ProductName = 'HogyunsVirtualDrum',
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# 이미 개발자 셸 안이면 다시 불러오지 않는다.
if (-not $env:VSCMD_VER) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe not found. Install Visual Studio Build Tools with the C++ workload.'
    }

    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) {
        throw 'No Visual Studio installation with the C++ build tools was found.'
    }

    # 개발자 셸이 시작하면서 vswhere를 찾으므로 PATH에 넣어 둔다.
    $env:PATH = (Split-Path $vswhere) + ';' + $env:PATH

    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

$buildDir = Join-Path $root (Join-Path 'build' $Config.ToLower())

if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}

cmake -S $root -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DHVD_PRODUCT_NAME=$ProductName"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $buildDir "HogyunsVirtualDrum_artefacts\$Config\$ProductName.exe"
"Built: $exe"

if ($Run) {
    Start-Process -FilePath $exe
}
