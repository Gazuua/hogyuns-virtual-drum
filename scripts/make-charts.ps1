# 음악 폴더의 모든 곡에서 리듬 게임의 채보를 만든다. 프로그램 창을 띄우지 않고 뒤에서 돈다.
#
#   pwsh scripts/make-charts.ps1                     # 음악 폴더 전체. 이미 채보가 있는 곡은 건너뛴다
#   pwsh scripts/make-charts.ps1 -Folder "D:\노래"   # 다른 폴더
#   pwsh scripts/make-charts.ps1 -Status             # 진행 상황만 본다
#   pwsh scripts/make-charts.ps1 -Stop               # 멈춘다. 다시 실행하면 남은 곡부터 이어서 한다
#   pwsh scripts/make-charts.ps1 -Workers 4          # 함께 처리할 곡 수 (기본 3, 1 ~ 8)
#   pwsh scripts/make-charts.ps1 -Force              # 이미 있는 채보도 다시 만든다
#
# 이 창을 닫거나 Ctrl+C를 눌러도 작업은 뒤에서 계속된다. 멈추려면 -Stop을 쓴다.
#
# 채보를 만드는 실행 파일은 tools\chartmaker\HvdChartMaker.exe 다. 본 프로그램의 실행 파일을 복사해 둔 것이라서,
# 본 프로그램을 다시 빌드하는 중이어도 영향을 받지 않는다. 받아 적는 방법을 고친 새 빌드로 바꾸려면
# build\release\HogyunsVirtualDrum_artefacts\Release\HogyunsVirtualDrum.exe 를 그 자리에 다시 복사한다.
# (사본이 없으면 본 프로그램의 실행 파일을 그대로 쓴다.)
#
# 채보와 드럼을 뺀 반주는 charts\ 에 쌓인다 (곡당 약 5MB). 진행 상황은 charts\batch-progress.txt 에 적힌다.
param(
    [string]$Folder,
    [ValidateRange(1, 8)] [int]$Workers = 3,
    [switch]$Force,
    [switch]$Stop,
    [switch]$Status,
    [switch]$NoWatch
)

$root = Split-Path -Parent $PSScriptRoot
$tool = Join-Path $root 'tools\chartmaker\HvdChartMaker.exe'

if (-not (Test-Path $tool)) {
    $tool = Join-Path $root 'build\release\HogyunsVirtualDrum_artefacts\Release\HogyunsVirtualDrum.exe'
}

$charts = Join-Path $root 'charts'
New-Item -ItemType Directory -Force $charts | Out-Null

$progress = Join-Path $charts 'batch-progress.txt'
$stopFile = "$progress.stop"
$pidFile = Join-Path $charts 'batch.pid'

function Show-Progress {
    if (Test-Path $progress) { Get-Content $progress -Encoding UTF8 } else { '아직 진행 기록이 없습니다.' }
}

# 지금 돌고 있는 작업 (시작할 때 적어 둔 프로세스 번호로 찾는다)
function Get-Batch {
    if (-not (Test-Path $pidFile)) { return $null }
    $id = [int](Get-Content $pidFile -TotalCount 1)
    $found = Get-Process -Id $id -ErrorAction SilentlyContinue
    if ($found -and $found.ProcessName -eq [IO.Path]::GetFileNameWithoutExtension($tool)) { return $found }
    return $null
}

if ($Status) {
    Show-Progress
    if (Get-Batch) { '(돌고 있습니다)' } else { '(돌고 있지 않습니다)' }
    exit 0
}

if ($Stop) {
    $batch = Get-Batch
    if (-not $batch) { '돌고 있는 작업이 없습니다.'; exit 0 }

    Set-Content -Path $stopFile -Value 'stop'
    '멈추는 중입니다...'
    $batch | Wait-Process -Timeout 60 -ErrorAction SilentlyContinue
    Show-Progress
    exit 0
}

$batch = Get-Batch

if ($batch) {
    '이미 돌고 있습니다. 진행 상황을 보여 줍니다.'
}
else {
    Remove-Item $stopFile, $progress -ErrorAction SilentlyContinue

    $arguments = @('--analyse-folder', "`"$progress`"", '--workers', $Workers)
    if ($Folder) { $arguments += @('--folder', "`"$((Resolve-Path -LiteralPath $Folder).Path)`"") }
    if ($Force) { $arguments += '--force' }

    try {
        $batch = Start-Process -FilePath $tool -ArgumentList $arguments -PassThru -ErrorAction Stop
    }
    catch {
        "실행하지 못했습니다: $($_.Exception.Message)"
        exit 125
    }

    Set-Content -Path $pidFile -Value $batch.Id
    "시작했습니다: $tool"
}

if ($NoWatch) { exit 0 }

# 끝날 때까지 5초마다 진행 상황을 다시 보여 준다.
while (-not $batch.HasExited) {
    Start-Sleep -Seconds 5
    Clear-Host
    Show-Progress
    ''
    '이 창을 닫거나 Ctrl+C를 눌러도 작업은 뒤에서 계속됩니다.'
    '멈추려면: pwsh scripts/make-charts.ps1 -Stop'
}

Clear-Host
Show-Progress
Remove-Item $pidFile -ErrorAction SilentlyContinue
