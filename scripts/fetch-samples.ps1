# 드럼 샘플 라이브러리를 external/sample-libraries 에 내려받는다.
# CC0 1.0(퍼블릭 도메인)이 다섯, CC BY 4.0이 셋, CC BY-SA(3.0, 4.0)가 셋이다. 출처는 README와 kits/default/CREDITS.txt.
#
#   pwsh scripts/fetch-samples.ps1                 # 아직 없는 것을 모두 받는다 (약 9GB)
#   pwsh scripts/fetch-samples.ps1 -Only frankensnare,naked-drums
#   pwsh scripts/fetch-samples.ps1 -List           # 무엇이 있고 없는지만 본다
#
# 받은 것은 원본 그대로 둔다. 킷에서 쓸 소리는 tools/build_sounds.py 가 여기서 골라 가공한다.
# 라이선스는 2026-11-25, 26에 GitHub의 저장소 정보와 각 폴더의 LICENSE(README) 파일로 확인했다.
# Salamander는 GitHub에 샘플이 없어 Internet Archive의 압축 파일을 받는다 (Url, 맨 위 폴더가 없다).
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
    @{ Name = 'virtuosity-drums';  Repo = 'sfzinstruments/virtuosity_drums';            Branch = 'master'; What = '재즈 킷 한 벌과 타악기' }
    @{ Name = 'swirly-drums';      Repo = 'sfzinstruments/karoryfer.swirly-drums';      Branch = 'main';   What = '브러시 킷과 손 타악기' }
    @{ Name = 'gogodze-phu-vol-ii'; Repo = 'sfzinstruments/karoryfer.gogodze-phu-vol-ii'; Branch = 'master'; What = '1970년대 소리를 노린 작은 킷 (킥, 스네어, 탐 셋, 하이햇)' }
    # 아래는 CC-BY 4.0(출처 표시) 또는 CC-BY-SA(출처 표시, 고친 것도 같은 조건). 출처는 README와 kits/default/CREDITS.txt에 적는다.
    @{ Name = 'naked-drums';       Repo = 'sfzinstruments/WilkinsonAudio.NakedDrums';   Branch = 'master'; What = 'Wilkinson Audio. 록 킷 한 벌, 번갈아 10번 (CC-BY 4.0)' }
    @{ Name = 'drs-kit';           Repo = 'sfzinstruments/DrumGizmo.DRSKit';            Branch = 'master'; What = 'DrumGizmo. 록 킷 한 벌 (CC-BY 4.0)' }
    @{ Name = 'muldjord-kit';      Repo = 'sfzinstruments/DrumGizmo.MuldjordKit';       Branch = 'master'; What = 'DrumGizmo. 록 킷 한 벌 (CC-BY 4.0)' }
    @{ Name = 'sams-sonor';        Repo = 'sfzinstruments/SamsSonor';                   Branch = 'master'; What = 'Sam Greene. Sonor Force 3001 킷 (CC-BY-SA 4.0)' }
    @{ Name = 'avl-drumkits';      Repo = 'studiorack/avl-drumkits';                    Branch = 'main';   What = 'AVL. Black Pearl, Red Zeppelin 두 킷 (CC-BY-SA 3.0)' }
    @{ Name = 'salamander-drumkit'; Url = 'https://archive.org/download/SalamanderDrumkit/salamanderDrumkit.tar.bz2'; Strip = 0; What = 'Alexander Holm. 록 킷 한 벌 (CC-BY-SA 3.0)' }
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

    $url = if ($library.Url) { $library.Url } else { "https://codeload.github.com/$($library.Repo)/zip/refs/heads/$($library.Branch)" }
    $zip = Join-Path $target ($library.Name + $(if ($library.Url) { [IO.Path]::GetExtension($library.Url) } else { '.zip' }))
    $strip = if ($null -ne $library.Strip) { $library.Strip } else { 1 }

    & curl.exe --location --fail --silent --show-error --retry 3 --retry-delay 5 --output $zip $url
    if ($LASTEXITCODE -ne 0) { throw "Download failed: $($library.Name)" }

    # 받다 만 폴더가 있으면 지우고, 새 빈 폴더에 푼다. GitHub 압축 파일 맨 위의 "저장소-브랜치" 폴더는 벗긴다.
    if (Test-Path $folder) { Remove-Item -Recurse -Force $folder }
    New-Item -ItemType Directory $folder | Out-Null

    & tar.exe -xf $zip -C $folder --strip-components $strip
    if ($LASTEXITCODE -ne 0) { throw "Could not unpack: $($library.Name)" }

    Set-Content -Path $marker -Value "$(if ($library.Url) { $library.Url } else { "$($library.Repo) @ $($library.Branch)" }), $(Get-Date -Format s)" -Encoding UTF8
    Remove-Item $zip

    $size = (Get-ChildItem $folder -Recurse -File | Measure-Object Length -Sum).Sum
    "{0,-18} 받음    {1:N0} MB  ({2})" -f $library.Name, ($size / 1MB), (Get-Date -Format HH:mm:ss)
}
