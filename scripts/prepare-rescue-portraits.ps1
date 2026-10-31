# Resample generated masters to the existing personnel portrait contract.
param([switch]$AvailableOnly)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = Split-Path $PSScriptRoot -Parent
$sourceDirectory = Join-Path $projectRoot 'docs/art/rescue-npc-v1'
$outputDirectory = Join-Path $projectRoot 'content/base/assets/personnel/rescue'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$manifest = Get-Content -LiteralPath (Join-Path $sourceDirectory 'manifest.json') -Raw | ConvertFrom-Json
$sheet = [System.Drawing.Bitmap]::new(1536, 960)
$sheetGraphics = [System.Drawing.Graphics]::FromImage($sheet)
try
{
	$sheetGraphics.Clear([System.Drawing.Color]::FromArgb(239, 221, 184))
	$slot = 0
	foreach ($person in $manifest.portraits)
	{
		if ($AvailableOnly -and !(Test-Path -LiteralPath (Join-Path $sourceDirectory $person.master)))
		{
			continue
		}
		$source = [System.Drawing.Image]::FromFile((Join-Path $sourceDirectory $person.master))
		$image = [System.Drawing.Bitmap]::new(192, 192)
		$graphics = [System.Drawing.Graphics]::FromImage($image)
		$attributes = [System.Drawing.Imaging.ImageAttributes]::new()
		try
		{
			if ($source.Width -ne $source.Height)
			{
				throw "Non-square portrait: $($person.id)"
			}
			$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
			$graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
			$attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
			$graphics.DrawImage($source, [System.Drawing.Rectangle]::new(0, 0, 192, 192), 0, 0, $source.Width, $source.Height, [System.Drawing.GraphicsUnit]::Pixel, $attributes)
			$destination = Join-Path $outputDirectory $person.master
			$image.Save($destination, [System.Drawing.Imaging.ImageFormat]::Png)
			if ((Get-Item -LiteralPath $destination).Length -gt 262144)
			{
				throw "Portrait exceeds 256 KiB: $($person.id)"
			}
			$sheetGraphics.DrawImageUnscaled($image, ($slot % 8) * 192, [math]::Floor($slot / 8) * 192)
			$slot++
		}
		finally
		{
			$attributes.Dispose(); $graphics.Dispose(); $image.Dispose(); $source.Dispose()
		}
	}
	if (!$AvailableOnly -and $slot -ne 40)
	{
		throw "Expected 40 portraits, found $slot"
	}
	$sheet.Save((Join-Path $sourceDirectory 'contact-sheet.png'), [System.Drawing.Imaging.ImageFormat]::Png)
}
finally
{
	$sheetGraphics.Dispose(); $sheet.Dispose()
}
Write-Output "Prepared $slot runtime portraits at 192px and an eight-column contact sheet."
