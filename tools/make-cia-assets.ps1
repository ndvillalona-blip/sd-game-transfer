# Genera los recursos del CIA: banner.png (256x128), banner.wav e icon.icn.
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$out  = Join-Path $root "cia"
New-Item -ItemType Directory -Force $out | Out-Null

function New-RoundedPath($x, $y, $w, $h, $r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

# ---------------- banner 256x128 ----------------
$w = 256; $h = 128
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

$rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
$bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    $rect,
    [System.Drawing.Color]::FromArgb(255, 6, 9, 22),
    [System.Drawing.Color]::FromArgb(255, 28, 18, 58),
    30)
$g.FillRectangle($bg, $rect)

$cyan  = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 34, 224, 255))
$pink  = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 63, 164))
$white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 232, 240, 255))
$dark  = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 6, 9, 20))

# Brillos de fondo
$glowC = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(26, 34, 224, 255))
$glowP = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(22, 255, 63, 164))
$g.FillEllipse($glowC, 150, -40, 150, 150)
$g.FillEllipse($glowP, -50, 70, 150, 120)

# Logo
$logoPath = New-RoundedPath 18 30 68 68 16
$g.FillPath($cyan, $logoPath)
$fLogo = New-Object System.Drawing.Font("Segoe UI", 24, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$g.DrawString("SD", $fLogo, $dark, 31, 42)

# Textos
$fTitle = New-Object System.Drawing.Font("Segoe UI", 22, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$g.DrawString("SD GAME", $fTitle, $white, 100, 22)
$g.DrawString("TRANSFER", $fTitle, $cyan, 100, 47)
$fSub = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$g.DrawString("by frisk  -  2DS / 3DS", $fSub, $pink, 101, 78)
$g.FillRectangle($cyan, 101, 97, 112, 2)
$g.FillRectangle($pink, 217, 97, 16, 2)

$g.Dispose()
$bmp.Save((Join-Path $out "banner.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

# ---------------- banner.wav (pitido suave) ----------------
$sr = 44100
$dur = 1.1
$n = [int]($sr * $dur)
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$bw.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
$bw.Write([int](36 + $n * 2))
$bw.Write([System.Text.Encoding]::ASCII.GetBytes("WAVEfmt "))
$bw.Write([int]16)
$bw.Write([int16]1)
$bw.Write([int16]1)
$bw.Write([int]$sr)
$bw.Write([int]($sr * 2))
$bw.Write([int16]2)
$bw.Write([int16]16)
$bw.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
$bw.Write([int]($n * 2))

for ($i = 0; $i -lt $n; $i++) {
    $t = $i / $sr
    $env = [Math]::Min($t / 0.02, 1.0) * [Math]::Min(($dur - $t) / 0.25, 1.0)
    $f = 660.0 + 220.0 * ($t / $dur)
    $s = [Math]::Sin(2 * [Math]::PI * $f * $t) * 0.22 + [Math]::Sin(2 * [Math]::PI * $f * 2 * $t) * 0.06
    $v = [int16]([Math]::Max(-1.0, [Math]::Min(1.0, $s * $env)) * 32767)
    $bw.Write($v)
}
$bw.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $out "banner.wav"), $ms.ToArray())
$bw.Dispose(); $ms.Dispose()

Write-Output "OK: $out\banner.png y banner.wav"
