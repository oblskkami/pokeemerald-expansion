# 把 build\windows 里“能直接跑游戏”的文件打包成可分发 zip。
# 用法（任意目录均可）：
#   powershell -ExecutionPolicy Bypass -File tools\package_windows.ps1
#   powershell -ExecutionPolicy Bypass -File tools\package_windows.ps1 -Name "绿宝石改版PC版"
param(
    [string]$Name = "PokemonEmerald-PC"
)

$ErrorActionPreference = "Stop"
# 在非交互 shell 里 Compress-Archive 的进度条会读控制台并报 0x5，直接关掉。
$ProgressPreference = "SilentlyContinue"

$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root "build\windows"
$exe  = Join-Path $src  "pokeemerald.exe"
$dll  = Join-Path $src  "SDL2.dll"
$readmeSrc = Join-Path $root "docs\pc\操作说明.txt"

foreach ($p in @($exe, $dll, $readmeSrc)) {
    if (-not (Test-Path $p)) { throw "缺少文件：$p" }
}

$running = Get-Process pokeemerald -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "检测到游戏正在运行，先关掉再打包。" -ForegroundColor Yellow
    $running | Stop-Process -Force
    Start-Sleep -Milliseconds 500
}

$dist = Join-Path $root "dist"
$out  = Join-Path $dist $Name
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Path $out -Force | Out-Null

Copy-Item $exe $out
Copy-Item $dll $out
Copy-Item $readmeSrc (Join-Path $out "操作说明.txt")

# 只带 exe / dll / 说明，绝不带存档 pokeemerald.sav。
$stamp = Get-Date -Format "yyyyMMdd"
$zip = Join-Path $dist ("$Name-$stamp.zip")
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $out "*") -DestinationPath $zip

Write-Host ""
Write-Host ("打包完成：{0}" -f $zip)
Write-Host ("大小：{0:N1} MB" -f ((Get-Item $zip).Length / 1MB))
Write-Host "zip 内容："
Get-ChildItem $out | ForEach-Object { Write-Host ("  {0}  {1:N0} 字节" -f $_.Name, $_.Length) }
Write-Host ""
Write-Host "提醒：zip 里不含存档 pokeemerald.sav，发出去不会带走你的存档。"
