# Generates src/TrafficGate.ico: Two clean white traffic arrows on a red rounded tile, matching PepperSnap aesthetic.
#
#   powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1

param(
    [string]$Output = (Join-Path $PSScriptRoot '..\src\TrafficGate.ico')
)

Add-Type -AssemblyName System.Drawing

function New-Artwork([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    $s = [double]$size

    # Red rounded tile (PepperSnap gradient) ----------------------------------
    $inset = $s * 0.035
    $tileSize = $s - 2 * $inset
    $tileRect = New-Object System.Drawing.RectangleF($inset, $inset, $tileSize, $tileSize)
    $radius = $s * 0.22
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2
    $path.AddArc($tileRect.X, $tileRect.Y, $d, $d, 180, 90)
    $path.AddArc($tileRect.Right - $d, $tileRect.Y, $d, $d, 270, 90)
    $path.AddArc($tileRect.Right - $d, $tileRect.Bottom - $d, $d, $d, 0, 90)
    $path.AddArc($tileRect.X, $tileRect.Bottom - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    $tileBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        $tileRect,
        [System.Drawing.Color]::FromArgb(255, 240, 68, 68),
        [System.Drawing.Color]::FromArgb(255, 226, 52, 52),
        90.0)
    $g.FillPath($tileBrush, $path)

    # Two Traffic Arrows -----------------------------------------------------
    # Left arrow: points DOWN
    # Right arrow: points UP
    $strokeWidth = [math]::Max(2.0, $s * 0.082)
    $arrowPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 255, 255, 255), $strokeWidth)
    $arrowPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $arrowPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $arrowPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round

    $leftX = $s * 0.355
    $rightX = $s * 0.645
    $topY = $s * 0.21
    $botY = $s * 0.79
    $headLen = $s * 0.175

    # Left arrow (pointing down)
    # Stem
    $g.DrawLine($arrowPen, [float]$leftX, [float]$topY, [float]$leftX, [float]$botY)
    # Left barb
    $g.DrawLine($arrowPen, [float]$leftX, [float]$botY, [float]($leftX - $headLen), [float]($botY - $headLen))
    # Right barb
    $g.DrawLine($arrowPen, [float]$leftX, [float]$botY, [float]($leftX + $headLen), [float]($botY - $headLen))

    # Right arrow (pointing up)
    # Stem
    $g.DrawLine($arrowPen, [float]$rightX, [float]$botY, [float]$rightX, [float]$topY)
    # Left barb
    $g.DrawLine($arrowPen, [float]$rightX, [float]$topY, [float]($rightX - $headLen), [float]($topY + $headLen))
    # Right barb
    $g.DrawLine($arrowPen, [float]$rightX, [float]$topY, [float]($rightX + $headLen), [float]($topY + $headLen))

    $arrowPen.Dispose()
    $tileBrush.Dispose()
    $path.Dispose()
    $g.Dispose()

    return $bmp
}

function Get-PngBytes([System.Drawing.Bitmap]$bmp) {
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $ms.ToArray()
    $ms.Dispose()
    return $bytes
}

function Get-DibBytes([System.Drawing.Bitmap]$bmp) {
    $w = $bmp.Width
    $h = $bmp.Height
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $pixels = New-Object byte[] ($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $pixels, 0, $pixels.Length)
    $bmp.UnlockBits($data)

    $maskStride = [int](([math]::Floor(($w + 31) / 32)) * 4)
    $xorSize = $w * 4 * $h
    $andSize = $maskStride * $h
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)

    $bw.Write([int32]40); $bw.Write([int32]$w); $bw.Write([int32]($h * 2))
    $bw.Write([int16]1); $bw.Write([int16]32); $bw.Write([int32]0)
    $bw.Write([int32]($xorSize + $andSize))
    $bw.Write([int32]0); $bw.Write([int32]0); $bw.Write([int32]0); $bw.Write([int32]0)

    for ($y = $h - 1; $y -ge 0; $y--) {
        $row = $y * $stride
        for ($x = 0; $x -lt $w; $x++) {
            $i = $row + $x * 4
            $bw.Write([byte]$pixels[$i + 0])
            $bw.Write([byte]$pixels[$i + 1])
            $bw.Write([byte]$pixels[$i + 2])
            $bw.Write([byte]$pixels[$i + 3])
        }
    }
    $bw.Write((New-Object byte[] $andSize))

    $bw.Flush()
    $bytes = $ms.ToArray()
    $bw.Dispose(); $ms.Dispose()
    return $bytes
}

$sizes = @(16, 24, 32, 48, 64, 128)
$frames = New-Object System.Collections.Generic.List[object]
foreach ($size in $sizes) {
    $big = New-Artwork ($size * 4)
    $small = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $sg = [System.Drawing.Graphics]::FromImage($small)
    $sg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $sg.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $sg.Clear([System.Drawing.Color]::Transparent)
    $sg.DrawImage($big, (New-Object System.Drawing.Rectangle(0, 0, $size, $size)),
                  (New-Object System.Drawing.Rectangle(0, 0, $big.Width, $big.Height)),
                  [System.Drawing.GraphicsUnit]::Pixel)
    $sg.Dispose(); $big.Dispose()

    $frames.Add(@{ Size = $size; Bytes = [byte[]](Get-DibBytes $small); BitCount = 32 })
    $small.Dispose()
}

$pngBmp = New-Artwork 256
$frames.Add(@{ Size = 256; Bytes = [byte[]](Get-PngBytes $pngBmp); BitCount = 32 })

# Preview PNG
$docs = Join-Path (Split-Path $PSScriptRoot -Parent) 'docs'
New-Item -ItemType Directory -Force -Path $docs | Out-Null
$preview = New-Artwork 256
$preview.Save((Join-Path $docs 'icon.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$preview.Dispose(); $pngBmp.Dispose()

# Save ICO
$parent = Split-Path $Output -Parent
New-Item -ItemType Directory -Force -Path $parent | Out-Null

$stream = [System.IO.File]::Create($Output)
$bw = New-Object System.IO.BinaryWriter($stream)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$frames.Count)

$offset = 6 + 16 * $frames.Count
foreach ($frame in $frames) {
    $dim = if ($frame.Size -ge 256) { 0 } else { $frame.Size }
    $bw.Write([byte]$dim); $bw.Write([byte]$dim)
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]$frame.BitCount)
    $bw.Write([uint32]$frame.Bytes.Length)
    $bw.Write([uint32]$offset)
    $offset += $frame.Bytes.Length
}
foreach ($frame in $frames) {
    $bytes = [byte[]]$frame.Bytes
    $bw.Write($bytes, 0, $bytes.Length)
}
$bw.Flush()
$stream.Flush($true)
$bw.Dispose(); $stream.Dispose()

$icon = New-Object System.Drawing.Icon($Output)
$bytes = [System.IO.File]::ReadAllBytes($Output)
"Generated $Output ($([math]::Round($bytes.Length / 1KB, 1)) KB, $($frames.Count) frames, primary $($icon.Width)x$($icon.Height))"
$icon.Dispose()
