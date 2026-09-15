param(
    [string]$SourceDirectory = (Join-Path $PSScriptRoot "..\assets\models\environment\grass_pack")
)

$sourcePath = Join-Path $SourceDirectory "scene.gltf"
$source = Get-Content -LiteralPath $sourcePath -Raw | ConvertFrom-Json

$variants = @{
    "grass_aged.gltf" = 4
    "grass_dry.gltf" = 10
    "grass_fresh.gltf" = 16
}

foreach ($variant in $variants.GetEnumerator()) {
    $document = Get-Content -LiteralPath $sourcePath -Raw | ConvertFrom-Json
    $document.nodes[2].children = @(3)
    $document.nodes[3].children = @($variant.Value)
    $outputPath = Join-Path $SourceDirectory $variant.Name
    $document | ConvertTo-Json -Depth 100 -Compress | Set-Content -LiteralPath $outputPath -Encoding utf8NoBOM
}
