# 빌드한 뒤 코어 자동 테스트를 돌린다.
# 테스트는 본 프로그램에 들어 있고 `--coretests <보고서>` 옵션으로 실행한다.
#
#   pwsh scripts/test.ps1
#   pwsh scripts/test.ps1 -Config Debug
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Config = 'Release',
    [string]$ProductName = 'HogyunsVirtualDrum'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

& (Join-Path $PSScriptRoot 'build.ps1') -Config $Config -ProductName $ProductName
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $root "build\$($Config.ToLower())\HogyunsVirtualDrum_artefacts\$Config\$ProductName.exe"
$report = Join-Path $root "build\$($Config.ToLower())\coretests.txt"
Remove-Item $report -ErrorAction SilentlyContinue

$process = Start-Process -FilePath $exe -ArgumentList @('--coretests', "`"$report`"") -PassThru -Wait
$code = $process.ExitCode

if (Test-Path $report) { Get-Content $report -Encoding UTF8 }
exit $code
