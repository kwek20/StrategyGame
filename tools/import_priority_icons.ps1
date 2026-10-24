param(
    [Parameter(Mandatory=$true)][string]$LowImage,
    [Parameter(Mandatory=$true)][string]$MediumImage,
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$sourceRoot = Join-Path $ProjectRoot 'assets/icons/source'
$generatedRoot = Join-Path $sourceRoot 'generated'
$originalRoot = Join-Path $generatedRoot 'priority-originals'
New-Item -ItemType Directory -Path $originalRoot -Force | Out-Null

function Get-IconBounds($bitmap) {
    $left=$bitmap.Width; $top=$bitmap.Height; $right=-1; $bottom=-1
    for ($y=0; $y -lt $bitmap.Height; ++$y) {
        for ($x=0; $x -lt $bitmap.Width; ++$x) {
            if ($bitmap.GetPixel($x,$y).A -le 8) { continue }
            $left=[Math]::Min($left,$x); $top=[Math]::Min($top,$y)
            $right=[Math]::Max($right,$x); $bottom=[Math]::Max($bottom,$y)
        }
    }
    if ($right -lt $left) { throw 'Empty priority icon' }
    return [Drawing.Rectangle]::FromLTRB($left,$top,$right+1,$bottom+1)
}

$high=[Drawing.Bitmap]::new((Join-Path $sourceRoot 'action_power_priority.png'))
$reference=Get-IconBounds $high
foreach ($entry in @(@('low',$LowImage), @('medium',$MediumImage))) {
    $name='action_power_priority_'+$entry[0]+'.png'
    Copy-Item -LiteralPath $entry[1] -Destination (Join-Path $originalRoot $name) -Force
    $inputIcon=[Drawing.Bitmap]::new($entry[1])
    $bounds=Get-IconBounds $inputIcon
    $width=$reference.Width
    $height=[int][Math]::Round($bounds.Height*$width/$bounds.Width)
    $outputIcon=[Drawing.Bitmap]::new(256,256,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics=[Drawing.Graphics]::FromImage($outputIcon)
    $graphics.Clear([Drawing.Color]::Transparent)
    $graphics.CompositingMode=[Drawing.Drawing2D.CompositingMode]::SourceCopy
    $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.DrawImage($inputIcon,
        [Drawing.Rectangle]::new($reference.Left,$reference.Bottom-$height,$width,$height),
        $bounds,[Drawing.GraphicsUnit]::Pixel)
    $graphics.Dispose()
    $outputIcon.Save((Join-Path $generatedRoot $name),[Drawing.Imaging.ImageFormat]::Png)
    Copy-Item -LiteralPath (Join-Path $generatedRoot $name) -Destination (Join-Path $sourceRoot $name) -Force
    $outputIcon.Dispose(); $inputIcon.Dispose()
}
$high.Dispose()
