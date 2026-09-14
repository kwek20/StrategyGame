param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sourceRoot = Join-Path $ProjectRoot 'assets/icons/source'
$masterPath = Join-Path $sourceRoot 'master_icons_v2.png'
$atlasPath = Join-Path $ProjectRoot 'assets/textures/icons/icons_atlas.png'
$names = @(
    @('unit_worker','unit_construction_drone','building_town_center','building_command_hub','building_alloy_processor','building_fuel_processor','building_synthetic_mine','building_basic_generator'),
    @('building_outpost','building_storage_silo','building_electricity_pole','building_charging_pad','building_resource_extractor','building_drone_factory','building_sensor_tower','resource_scrap'),
    @('resource_oil','resource_uranium','resource_synthetic','resource_alloy','resource_fuel','resource_data','resource_authority','resource_power'),
    @('upgrade_efficient_training','upgrade_town_center_level_2','action_move','action_gather','action_construct','action_stop','action_repair','action_cancel_queue'),
    @('action_power_connect','action_power_disconnect','action_power_priority','status_constructing','status_damaged','status_low_battery','status_charging','status_powered'),
    @('status_unpowered','status_underpowered','status_power_blocked','status_grid_island','status_storage_charging','status_storage_discharging','status_processing','status_input_full')
)

# The generated contact sheet does not use equal-height rows. These boundaries are the
# transparent gaps between its six visual rows; treating the sheet as a 6x8 equal grid
# is what previously clipped the bottoms and shifted the first four atlas rows.
$rowEdges = @(0, 211, 421, 590, 748, 908, 1086)
$columnWidth = 181
$sourceSize = 256
$safeExtent = 216 # 20px minimum source margin on the limiting axis.

function Get-AlphaBounds($bitmap) {
    $minX = $bitmap.Width; $minY = $bitmap.Height; $maxX = -1; $maxY = -1
    for ($y = 0; $y -lt $bitmap.Height; ++$y) {
        for ($x = 0; $x -lt $bitmap.Width; ++$x) {
            if ($bitmap.GetPixel($x, $y).A -le 8) { continue }
            $minX = [Math]::Min($minX, $x); $minY = [Math]::Min($minY, $y)
            $maxX = [Math]::Max($maxX, $x); $maxY = [Math]::Max($maxY, $y)
        }
    }
    if ($maxX -lt $minX -or $maxY -lt $minY) { throw 'Icon region is empty' }
    return [Drawing.Rectangle]::FromLTRB($minX, $minY, $maxX + 1, $maxY + 1)
}

function New-NormalizedIcon($iconBitmap) {
    $bounds = Get-AlphaBounds $iconBitmap
    $scale = [Math]::Min($safeExtent / $bounds.Width, $safeExtent / $bounds.Height)
    $width = [int][Math]::Round($bounds.Width * $scale)
    $height = [int][Math]::Round($bounds.Height * $scale)
    $left = [int][Math]::Round(($sourceSize - $width) * 0.5)
    $top = [int][Math]::Round(($sourceSize - $height) * 0.5)
    $output = [Drawing.Bitmap]::new($sourceSize, $sourceSize,
        [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($output)
    $graphics.Clear([Drawing.Color]::Transparent)
    $graphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
    $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.DrawImage($iconBitmap, [Drawing.Rectangle]::new($left, $top, $width, $height),
        $bounds, [Drawing.GraphicsUnit]::Pixel)
    $graphics.Dispose()
    return [pscustomobject]@{ Bitmap = $output }
}

$master = [Drawing.Bitmap]::new($masterPath)
for ($row = 0; $row -lt $names.Count; ++$row) {
    for ($column = 0; $column -lt 8; ++$column) {
        $name = $names[$row][$column]
        # This source was repaired independently because the contact-sheet version had
        # incomplete base geometry. Keep the repaired source as the canonical override.
        if ($name -eq 'building_storage_silo') { continue }
        $cellHeight = $rowEdges[$row + 1] - $rowEdges[$row]
        $cell = [Drawing.Bitmap]::new($columnWidth, $cellHeight,
            [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $cellGraphics = [Drawing.Graphics]::FromImage($cell)
        $cellGraphics.Clear([Drawing.Color]::Transparent)
        $cellGraphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
        $cellGraphics.DrawImage($master,
            [Drawing.Rectangle]::new(0, 0, $columnWidth, $cellHeight),
            [Drawing.Rectangle]::new($column * $columnWidth, $rowEdges[$row],
                $columnWidth, $cellHeight), [Drawing.GraphicsUnit]::Pixel)
        $cellGraphics.Dispose()
        $normalized = (New-NormalizedIcon $cell).Bitmap
        $normalized.Save((Join-Path $sourceRoot ($name + '.png')),
            [Drawing.Imaging.ImageFormat]::Png)
        $normalized.Dispose(); $cell.Dispose()
    }
}
$master.Dispose()

# Keep compatibility source files valid as well as their semantic atlas aliases.
$sourceAliases = @{
    'building_material_processor' = 'building_alloy_processor'
    'resource_wood' = 'resource_scrap'
    'resource_stone' = 'resource_oil'
    'resource_gold' = 'resource_uranium'
    'resource_materials' = 'resource_alloy'
    'resource_components' = 'resource_data'
}
foreach ($alias in $sourceAliases.GetEnumerator()) {
    $aliasSource = Join-Path $sourceRoot ($alias.Value + '.png')
    $aliasDestination = Join-Path $sourceRoot ($alias.Key + '.png')
    Copy-Item $aliasSource $aliasDestination -Force
}

$atlas = [Drawing.Bitmap]::new(512, 512, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
$atlasGraphics = [Drawing.Graphics]::FromImage($atlas)
$atlasGraphics.Clear([Drawing.Color]::Transparent)
$atlasGraphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
$atlasGraphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
$atlasGraphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
for ($row = 0; $row -lt $names.Count; ++$row) {
    for ($column = 0; $column -lt 8; ++$column) {
        $icon = [Drawing.Bitmap]::new((Join-Path $sourceRoot ($names[$row][$column] + '.png')))
        $atlasGraphics.DrawImage($icon, [Drawing.Rectangle]::new($column * 64, $row * 64, 64, 64),
            [Drawing.Rectangle]::new(0, 0, 256, 256), [Drawing.GraphicsUnit]::Pixel)
        $icon.Dispose()
    }
}
foreach ($entry in @(@('status_queue',0,6), @('status_locked',1,6))) {
    $icon = [Drawing.Bitmap]::new((Join-Path $sourceRoot ($entry[0] + '.png')))
    $normalized = (New-NormalizedIcon $icon).Bitmap
    $atlasGraphics.DrawImage($normalized,
        [Drawing.Rectangle]::new(([int]$entry[1]) * 64, ([int]$entry[2]) * 64, 64, 64),
        [Drawing.Rectangle]::new(0, 0, 256, 256), [Drawing.GraphicsUnit]::Pixel)
    $normalized.Dispose(); $icon.Dispose()
}
$atlasGraphics.Dispose()
$atlas.Save($atlasPath, [Drawing.Imaging.ImageFormat]::Png)
$atlas.Dispose()
Write-Output "Rebuilt $atlasPath from centered, safe-area source icons."
