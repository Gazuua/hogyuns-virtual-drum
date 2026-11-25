# 드럼 샘플 라이브러리를 external/sample-libraries 에 내려받는다. 모두 CC0 1.0(퍼블릭 도메인)이다.
#
#   pwsh scripts/fetch-samples.ps1                 # 아직 없는 것을 모두 받는다 (약 6GB)
#   pwsh scripts/fetch-samples.ps1 -Only frankensnare,unruly-drums
#   pwsh scripts/fetch-samples.ps1 -List           # 무엇이 있고 없는지만 본다
#
# 받은 것은 원본 그대로 둔다. 킷에서 쓸 소리는 tools/build_sounds.py 가 여기서 골라 가공한다.
# 라이선스는 2026-11-25에 GitHub의 저장소 정보로 확인했다 (각 폴더의 LICENSE 파일에도 있다).
param(
    [string[]]$Only,
    [switch]$List
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$target = Join-Path $root 'external\sample-libraries'

$libraries = @(
    @{ Name = 'big-rusty-drums';   Repo = 'sfzinstruments/karoryfer.big-rusty-drums';   Branch = 'main';   What = '기본 킷의 원본. 록 킷 한 벌' }
    @{ Name = 'frankensnare';      Repo = 'sfzinstruments/karoryfer.frankensnare';      Branch = 'main';   What = '여러 스네어 모음' }
    @{ Name = 'unruly-drums';      Repo = 'sfzinstruments/karoryfer.unruly-drums';      Branch = 'main';   What = '모든 드럼에 스네어 와이어를 단 킷' }
    @{ Name = 'virtuosity-drums';  Repo = 'sfzinstruments/virtuosity_drums';            Branch = 'master'; What = '재즈 킷 한 벌과 타악기' }
    @{ Name = 'swirly-drums';      Repo = 'sfzinstruments/karoryfer.swirly-drums';      Branch = 'main';   What = '브러시 킷과 손 타악기' }
    @{ Name = 'hat-with-the-phat'; Repo = 'sfzinstruments/Karoryfer.TheHatWithThePhat'; Branch = 'master'; What = '라이드 두 장으로 만든 큰 하이햇' }
)

if ($Only) {
    $unknown = @($Only | Where-Object { $_ -notin $libraries.Name })
    if ($unknown) { throw "Unknown library: $($unknown -join ', '). Known: $($libraries.Name -join ', ')" }
    $libraries = @($libraries | Where-Object { $_.Name -in $Only })
}

New-Item -ItemType Directory -Force $target | Out-Null

foreach ($library in $libraries) {
    $folder = Join-Path $target $library.Name
    $marker = Join-Path $folder '.complete'

    if (Test-Path $marker) {
        "{0,-18} 있음    {1}" -f $library.Name, $library.What
        continue
    }

    if ($List) {
        "{0,-18} 없음    {1}" -f $library.Name, $library.What
        continue
    }

    "{0,-18} 받는 중 {1}  ({2})" -f $library.Name, $library.What, (Get-Date -Format HH:mm:ss)

    $zip = Join-Path $target "$($library.Name).zip"
    $url = "https://codeload.github.com/$($library.Repo)/zip/refs/heads/$($library.Branch)"

    & curl.exe --location --fail --silent --show-error --retry 3 --retry-delay 5 --output $zip $url
    if ($LASTEXITCODE -ne 0) { throw "Download failed: $($library.Name)" }

    # 받다 만 폴더가 있으면 지우고, 새 빈 폴더에 푼다. 압축 파일 맨 위의 "저장소-브랜치" 폴더는 벗긴다.
    if (Test-Path $folder) { Remove-Item -Recurse -Force $folder }
    New-Item -ItemType Directory $folder | Out-Null

    & tar.exe -xf $zip -C $folder --strip-components 1
    if ($LASTEXITCODE -ne 0) { throw "Could not unpack: $($library.Name)" }

    Set-Content -Path $marker -Value "$($library.Repo) @ $($library.Branch), $(Get-Date -Format s)" -Encoding UTF8
    Remove-Item $zip

    $size = (Get-ChildItem $folder -Recurse -File | Measure-Object Length -Sum).Sum
    "{0,-18} 받음    {1:N0} MB  ({2})" -f $library.Name, ($size / 1MB), (Get-Date -Format HH:mm:ss)
}
