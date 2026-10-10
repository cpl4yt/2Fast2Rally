# Makes the outer (corner-connected) near-white background of an image
# transparent and saves it as a 32-bit PNG. Interior white areas enclosed by
# the artwork are kept.
#   powershell -ExecutionPolicy Bypass -File tools\maketransparent.ps1 -In newlogo.png -Out res\logo.png
param([string]$In, [string]$Out, [int]$Threshold = 232)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile((Resolve-Path $In))
$w = $src.Width; $h = $src.Height
$bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.DrawImage($src, 0, 0, $w, $h); $g.Dispose(); $src.Dispose()
$rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadWrite, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$bytes = New-Object byte[] ($data.Stride * $h)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
$stride = $data.Stride
$seen = New-Object bool[] ($w * $h)
$queue = New-Object System.Collections.Generic.Queue[int]
foreach ($p in @(0, ($w - 1), (($h - 1) * $w), (($h - 1) * $w + $w - 1))) { $queue.Enqueue($p) }
while ($queue.Count -gt 0) {
    $p = $queue.Dequeue()
    if ($seen[$p]) { continue }
    $seen[$p] = $true
    $x = $p % $w; $y = [math]::Floor($p / $w)
    $o = $y * $stride + $x * 4
    if ($bytes[$o] -lt $Threshold -or $bytes[$o + 1] -lt $Threshold -or $bytes[$o + 2] -lt $Threshold) { continue }
    $bytes[$o + 3] = 0
    if ($x -gt 0) { $queue.Enqueue($p - 1) }
    if ($x -lt $w - 1) { $queue.Enqueue($p + 1) }
    if ($y -gt 0) { $queue.Enqueue($p - $w) }
    if ($y -lt $h - 1) { $queue.Enqueue($p + $w) }
}
[System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
$bmp.UnlockBits($data)
$bmp.Save((Join-Path (Get-Location) $Out), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "Wrote $Out ($w x $h, transparent background)"
