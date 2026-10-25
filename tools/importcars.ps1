# Imports the car models from the "LowPoly_Cars_01" pack into res/models/cars/.
#   powershell -ExecutionPolicy Bypass -File tools\importcars.ps1 -Pack "..\asset packs"
# Needs w64devkit (g++) on PATH or at C:\w64devkit, and System.Drawing (Windows).
# Writes, per game car: <car>_body.obj, <car>_lower.obj (everything below the
# window line, for the in-car view), <car>_wheel.obj and <car>.json (measurements),
# plus one shared colour palette, atlas.png.
param([string]$Pack = "$PSScriptRoot\..\..\asset packs", [string]$Out = "$PSScriptRoot\..\res\models\cars")

$ErrorActionPreference = 'Stop'
$env:PATH = "C:\w64devkit\bin;$env:PATH"
$obj = Join-Path $Pack "LowPoly_Cars_01_obj.obj"
if (-not (Test-Path $obj)) { throw "Not found: $obj" }
New-Item -ItemType Directory -Force $Out | Out-Null

$tmp = Join-Path ([IO.Path]::GetTempPath()) "carsplit-build"
New-Item -ItemType Directory -Force $tmp | Out-Null
g++ -O2 "$PSScriptRoot\carsplit.cpp" -o "$tmp\carsplit.exe"

# Game car -> vehicle in the pack (index from `carsplit --list`).
$map = [ordered]@{ kestrel = 48; bianca = 43; vespera = 49; imperia = 45; quattra = 46; mito = 41; vortex = 44; escala = 42; banshee = 29 }
foreach ($car in $map.Keys) {
    Write-Host "== $car (pack vehicle $($map[$car]))"
    & "$tmp\carsplit.exe" $obj --export $map[$car] (Join-Path $Out $car)
}

# The pack's palette texture, shrunk (it is smooth gradients, so 512 is plenty).
$zip = Join-Path $Pack "textures.rar"
$jpg = Join-Path $tmp "textures\color_1024x1024.jpg"
if (-not (Test-Path $jpg)) { & "$env:SystemRoot\System32\tar.exe" -xf $zip -C $tmp }   # Windows bsdtar reads .rar; w64devkit's GNU tar does not
Add-Type -AssemblyName System.Drawing
$img = [Drawing.Image]::FromFile($jpg)
$bmp = New-Object Drawing.Bitmap 512, 512
$g = [Drawing.Graphics]::FromImage($bmp); $g.InterpolationMode = 'HighQualityBicubic'; $g.DrawImage($img, 0, 0, 512, 512)
$bmp.Save((Join-Path $Out "atlas.png"), [Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose(); $img.Dispose()
Write-Host "Done -> $Out"
