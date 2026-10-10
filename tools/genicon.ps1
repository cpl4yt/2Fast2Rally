# Writes res/icon.png (window icon) and res/icon.ico (exe icon, PNG entries at
# several sizes). By default it draws the built-in "2F2R" badge; pass your own
# square artwork to use that instead:
#   powershell -ExecutionPolicy Bypass -File tools\genicon.ps1
#   powershell -ExecutionPolicy Bypass -File tools\genicon.ps1 -FromPng path\to\my_icon.png
# Then rebuild so the exe picks up the new icon.
param([string]$FromPng = "")
Add-Type -AssemblyName System.Drawing
$root = Join-Path $PSScriptRoot '..'
$source = $null
if ($FromPng -ne "") { $source = [System.Drawing.Image]::FromFile((Resolve-Path $FromPng)) }

function Draw-Icon([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.Clear([System.Drawing.Color]::Transparent)
    if ($source) { $g.DrawImage($source, 0, 0, $size, $size); $g.Dispose(); return $bmp }
    $s = $size / 256.0
    # Rounded dark tile.
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = 48 * $s; $w = $size - 1
    $path.AddArc(0, 0, $r, $r, 180, 90); $path.AddArc($w - $r, 0, $r, $r, 270, 90)
    $path.AddArc($w - $r, $w - $r, $r, $r, 0, 90); $path.AddArc(0, $w - $r, $r, $r, 90, 90); $path.CloseFigure()
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 22, 24, 30))), $path)
    # Checkered band.
    $cell = 16 * $s
    for ($x = 0; $x -lt 16; $x++) { for ($y = 0; $y -lt 2; $y++) {
        $c = if ((($x + $y) % 2) -eq 0) { [System.Drawing.Color]::White } else { [System.Drawing.Color]::FromArgb(255, 30, 30, 30) }
        $g.FillRectangle((New-Object System.Drawing.SolidBrush $c), [float]($x * $cell), [float](188 * $s + $y * $cell), [float]$cell, [float]$cell)
    } }
    # Speed stripes.
    $orange = [System.Drawing.Color]::FromArgb(255, 255, 176, 0)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush $orange), [float](20 * $s), [float](150 * $s), [float](216 * $s), [float](14 * $s))
    # "2F2R"
    $font = New-Object System.Drawing.Font('Bahnschrift', [float](92 * $s), [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat; $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $g.DrawString('2F2R', $font, (New-Object System.Drawing.SolidBrush $orange), (New-Object System.Drawing.RectangleF(0, [float](20 * $s), [float]$size, [float](120 * $s))), $fmt)
    $g.Dispose()
    return $bmp
}

$big = Draw-Icon 256
$big.Save((Join-Path $root 'res\icon.png'), [System.Drawing.Imaging.ImageFormat]::Png)

# ICO with PNG entries.
$sizes = 256, 64, 48, 32, 16
$pngs = foreach ($sz in $sizes) {
    $bmp = Draw-Icon $sz
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    , $ms.ToArray()
}
$out = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $out
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $sz = $sizes[$i]; $len = $pngs[$i].Length
    $bw.Write([byte]($(if ($sz -ge 256) { 0 } else { $sz }))); $bw.Write([byte]($(if ($sz -ge 256) { 0 } else { $sz })))
    $bw.Write([byte]0); $bw.Write([byte]0); $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$len); $bw.Write([UInt32]$offset); $offset += $len
}
foreach ($p in $pngs) { $bw.Write($p) }
[System.IO.File]::WriteAllBytes((Join-Path $root 'res\icon.ico'), $out.ToArray())
Write-Output "Wrote res/icon.png and res/icon.ico"
