# Prepare display/network sizes from the generated officer portrait masters.
# Windows asset-preparation helper; no creative image modifications.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = Split-Path $PSScriptRoot -Parent
$sourceDirectory = Join-Path $projectRoot 'docs/art/officer-avatars-v1'
$outputDirectory = Join-Path $projectRoot 'content/ui/menu'

function Write-ResizedPortrait($sourceImage, [int]$size, [string]$destination) {
    $bitmap = [System.Drawing.Bitmap]::new($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $attributes = [System.Drawing.Imaging.ImageAttributes]::new()
        try {
            $attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
            $graphics.DrawImage($sourceImage, [System.Drawing.Rectangle]::new(0, 0, $size, $size), 0, 0, $sourceImage.Width, $sourceImage.Height, [System.Drawing.GraphicsUnit]::Pixel, $attributes)
        } finally { $attributes.Dispose() }
        $bitmap.Save($destination, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}

$preview = [System.Drawing.Bitmap]::new(768, 576)
$previewGraphics = [System.Drawing.Graphics]::FromImage($preview)
try {
    for ($slot = 1; $slot -le 12; $slot++) {
        $sourceImage = [System.Drawing.Image]::FromFile((Join-Path $sourceDirectory "officer-$slot.png"))
        try {
            if ($sourceImage.Width -ne $sourceImage.Height) { throw "Officer $slot is not square" }
            foreach ($size in @(48, 192)) {
                $prefix = if ($size -eq 48) { 'avatar' } else { 'portrait' }
                Write-ResizedPortrait $sourceImage $size (Join-Path $outputDirectory "$prefix-$slot.png")
            }
        } finally { $sourceImage.Dispose() }
        $avatarPath = Join-Path $outputDirectory "avatar-$slot.png"
        if ((Get-Item -LiteralPath $avatarPath).Length -gt 12288) { throw "Avatar $slot exceeds 12 KiB" }
        $portrait = [System.Drawing.Image]::FromFile((Join-Path $outputDirectory "portrait-$slot.png"))
        try {
            $previewGraphics.DrawImageUnscaled($portrait, (($slot - 1) % 4) * 192, [math]::Floor(($slot - 1) / 4) * 192)
        } finally { $portrait.Dispose() }
    }
    $preview.Save((Join-Path $outputDirectory 'avatars-preview.png'), [System.Drawing.Imaging.ImageFormat]::Png)
} finally { $previewGraphics.Dispose(); $preview.Dispose() }
Write-Output 'Prepared 12 paired 48px avatars and 192px portraits, plus a 4-by-3 preview.'
