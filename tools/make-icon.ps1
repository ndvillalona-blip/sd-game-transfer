Add-Type -AssemblyName System.Drawing

$bmp = New-Object System.Drawing.Bitmap(48, 48)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

$rect = New-Object System.Drawing.Rectangle(0, 0, 48, 48)
$brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
    $rect,
    [System.Drawing.Color]::FromArgb(34, 224, 255),
    [System.Drawing.Color]::FromArgb(255, 63, 164),
    45)
$g.FillRectangle($brush, $rect)

$font = [System.Drawing.Font]::new('Consolas', 22, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$fmt = New-Object System.Drawing.StringFormat
$fmt.Alignment = [System.Drawing.StringAlignment]::Center
$fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
$box = New-Object System.Drawing.RectangleF(0, 0, 48, 48)
$g.DrawString('SD', $font, [System.Drawing.Brushes]::White, $box, $fmt)

$g.Dispose()
$bmp.Save((Join-Path $PSScriptRoot '..\icon.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output 'icono listo'
