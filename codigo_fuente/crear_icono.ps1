# Genera optimizalis.ico (16-256 px) con el logotipo de Optimizalis
Add-Type -AssemblyName System.Drawing
$out = Join-Path $PSScriptRoot "optimizalis.ico"
$sizes = 16, 24, 32, 48, 64, 128, 256
$pngs = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)
    $r = New-Object System.Drawing.RectangleF 0, 0, $s, $s
    $rad = $s * 0.28
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $rad * 2
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($s - $d, 0, $d, $d, 270, 90)
    $path.AddArc($s - $d, $s - $d, $d, $d, 0, 90)
    $path.AddArc(0, $s - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $c1 = [System.Drawing.Color]::FromArgb(255, 45, 212, 191)
    $c2 = [System.Drawing.Color]::FromArgb(255, 99, 102, 241)
    $lb = New-Object System.Drawing.Drawing2D.LinearGradientBrush $r, $c1, $c2, 45
    $g.FillPath($lb, $path)
    $sheenRect = New-Object System.Drawing.RectangleF 0, 0, $s, ($s * 0.55)
    $sheen = New-Object System.Drawing.Drawing2D.LinearGradientBrush $sheenRect, ([System.Drawing.Color]::FromArgb(70, 255, 255, 255)), ([System.Drawing.Color]::FromArgb(0, 255, 255, 255)), 90
    $g.SetClip($path)
    $g.FillRectangle($sheen, $sheenRect)
    $g.ResetClip()
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(245, 255, 255, 255)), ([Math]::Max(1.5, $s * 0.09))
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    $m = $s * 0.22
    $g.DrawArc($pen, $m, $m, $s - 2 * $m, $s - 2 * $m, 135, 270)
    $cx = $s * 0.5; $cy = $s * 0.5; $rr = $s * 0.17
    $pts = @()
    for ($i = 0; $i -lt 8; $i++) {
        $a = -[Math]::PI / 2 + $i * [Math]::PI / 4
        $q = if ($i % 2 -eq 0) { $rr } else { $rr * 0.3 }
        $pts += New-Object System.Drawing.PointF ([single]($cx + [Math]::Cos($a) * $q)), ([single]($cy + [Math]::Sin($a) * $q))
    }
    $g.FillPolygon([System.Drawing.Brushes]::White, [System.Drawing.PointF[]]$pts)
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += , $ms.ToArray()
    $bmp.Dispose()
}
$fs = [System.IO.File]::Create($out)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $len = $pngs[$i].Length
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$len); $bw.Write([UInt32]$offset)
    $offset += $len
}
foreach ($p in $pngs) { $bw.Write($p) }
$bw.Close()
Write-Output "Icono creado: $out"
