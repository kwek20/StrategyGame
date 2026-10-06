param(
    [string]$AuditDirectory = 'build/map-generation-audit',
    [uint32]$Seed = 1592594996,
    [int]$MapSize = 10,
    [string]$Output = 'build/map-generation-audit/resource-layouts.png'
)

# Plot the audit's generated terrain and allocation data; no game/runtime files are modified.
Add-Type -AssemblyName System.Drawing
$layouts = @('continental', 'open_plains', 'central_hill', 'rolling_hills', 'central_water', 'archipelago')
$colors = @{
    scrap_field = [System.Drawing.Color]::FromArgb(255, 217, 132, 61)
    oil_field = [System.Drawing.Color]::FromArgb(255, 77, 198, 239)
    uranium_field = [System.Drawing.Color]::FromArgb(255, 189, 224, 79)
}
$bitmap = [System.Drawing.Bitmap]::new(1800, 1320)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::FromArgb(22, 27, 32))
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$font = [System.Drawing.Font]::new('Segoe UI', 12)
$titleFont = [System.Drawing.Font]::new('Segoe UI', 18, [System.Drawing.FontStyle]::Bold)
$graphics.DrawString("Resource allocations | $MapSize x $MapSize chunks | seed $Seed", $titleFont, [System.Drawing.Brushes]::White, 22, 12)
$graphics.DrawString('Orange: Scrap   Blue: Oil   Green: Uranium   Squares: starts   Rings: fields   Labels: reserved capacity per player', $font, [System.Drawing.Brushes]::White, 22, 50)

try {
    for ($panel = 0; $panel -lt $layouts.Count; $panel++) {
        $name = $layouts[$panel]
        $prefix = Join-Path $AuditDirectory "$name-$MapSize-$Seed"
        $left = 20 + ($panel % 3) * 600
        $top = 95 + [Math]::Floor($panel / 3) * 610
        $graphics.DrawString($name, $titleFont, [System.Drawing.Brushes]::White, $left, $top)
        if (!(Test-Path -LiteralPath "$prefix-terrain.ppm")) { continue }
        $bytes = [System.IO.File]::ReadAllBytes("$prefix-terrain.ppm")
        $offset = 0
        for ($line = 0; $line -lt 3; $line++) {
            while ($bytes[$offset++] -ne 10) {}
        }
        $terrain = [System.Drawing.Bitmap]::new(240, 240)
        for ($z = 0; $z -lt 240; $z++) {
            for ($x = 0; $x -lt 240; $x++) {
                $terrain.SetPixel($x, $z, [System.Drawing.Color]::FromArgb($bytes[$offset], $bytes[$offset + 1], $bytes[$offset + 2]))
                $offset += 3
            }
        }
        $plotX = $left
        $plotY = $top + 40
        $plotSize = 550
        $graphics.DrawImage($terrain, [int]$plotX, [int]$plotY, $plotSize, $plotSize)
        $terrain.Dispose()
        $extent = $MapSize * 24.0
        $scale = $plotSize / $extent
        foreach ($field in (Import-Csv -LiteralPath "$prefix-fields.csv")) {
            $x = $plotX + ([double]$field.x / $extent + 0.5) * $plotSize
            $y = $plotY + ([double]$field.z / $extent + 0.5) * $plotSize
            $radius = [double]$field.radius * $scale
            $pen = [System.Drawing.Pen]::new($colors[$field.family], 2)
            $graphics.DrawEllipse($pen, [single]($x - $radius), [single]($y - $radius), [single](2 * $radius), [single](2 * $radius))
            $pen.Dispose()
            if ([int]$field.starting_player -gt 0) {
                $label = "P$($field.starting_player): $($field.capacity)"
                $measure = $graphics.MeasureString($label, $font)
                $labelX = [Math]::Min($plotX + $plotSize - $measure.Width, [Math]::Max($plotX, $x + $radius))
                $labelY = $y - $measure.Height
                $graphics.FillRectangle([System.Drawing.Brushes]::Black, [single]$labelX, [single]$labelY, $measure.Width, $measure.Height)
                $brush = [System.Drawing.SolidBrush]::new($colors[$field.family])
                $graphics.DrawString($label, $font, $brush, [single]$labelX, [single]$labelY)
                $brush.Dispose()
            }
        }
        foreach ($node in (Import-Csv -LiteralPath "$prefix-nodes.csv")) {
            $x = $plotX + ([double]$node.x / $extent + 0.5) * $plotSize
            $y = $plotY + ([double]$node.z / $extent + 0.5) * $plotSize
            $brush = [System.Drawing.SolidBrush]::new($colors[$node.family])
            $graphics.FillEllipse($brush, [single]($x - 4), [single]($y - 4), 8, 8)
            $brush.Dispose()
        }
        foreach ($start in (Import-Csv -LiteralPath "$prefix-starts.csv")) {
            $x = $plotX + ([double]$start.x / $extent + 0.5) * $plotSize
            $y = $plotY + ([double]$start.z / $extent + 0.5) * $plotSize
            $graphics.FillRectangle([System.Drawing.Brushes]::White, [single]($x - 5), [single]($y - 5), 10, 10)
            $graphics.DrawString("P$($start.player)", $font, [System.Drawing.Brushes]::White, [single]($x + 7), [single]$y)
        }
    }
    $bitmap.Save([System.IO.Path]::GetFullPath($Output), [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output ([System.IO.Path]::GetFullPath($Output))
} finally {
    $titleFont.Dispose()
    $font.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}
