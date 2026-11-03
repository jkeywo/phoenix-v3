#Requires -Version 7.0
param([ValidateSet('native', 'web', 'all')][string]$Target = 'all', [string]$VCRuntimeDirectory)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $repo 'build/demo-packages'
New-Item -ItemType Directory -Path $output -Force | Out-Null

# Explicit roots: never copy a build output directory containing tools/tests.
$assets = @('audio', 'console', 'helm', 'menu', 'messages', 'pfx', 'base.zip.clients',
	'base.zip', 'base.zip.menu.json', 'ComicNeue-OFL.txt', 'ComicNeue-Regular.ttf',
	'FONT-LICENSE.txt', 'HARFBUZZ-LICENSE.txt', 'LatoLatin-Regular.ttf', 'locales.json',
	'QR-LICENSE.txt', 'shaping-Cairo-Regular.ttf', 'shaping-LICENSE.txt', 'shaping-Poppins-Regular.ttf',
	'SpaceMono-OFL.txt', 'SpaceMono-Regular.ttf', 'UnifrakturCook-Bold.ttf', 'UnifrakturCook-OFL.txt',
	'WEBP-LICENSE.txt', 'WEBP-PATENTS.txt')

foreach ($kind in $(if ($Target -eq 'all')
		{
			@('native', 'web')
		}
		else
		{
			@($Target)
		}))
{
	$build = Join-Path $repo "build/demo-$kind"
	$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt') -Raw
	foreach ($feature in @('ENABLE_WORKSHOP', 'ENABLE_GM', 'ENABLE_MULTISHIP', 'ENABLE_MODS', 'BUNDLE_TEST_MISSIONS'))
	{
		if ($cache -notmatch "(?m)^PHOENIX_${feature}:BOOL=OFF\r?$")
		{
			throw "Expected demo feature PHOENIX_$feature=OFF in $build"
		}
	}
	$stage = Join-Path $output "phoenix-demo-$kind"
	# A unique directory prevents stale files without deleting existing artifacts.
	$stage = "$stage-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))"
	New-Item -ItemType Directory -Path "$stage/assets" | Out-Null
	$assetSource = if ($kind -eq 'native')
	{
		"$build/Release/assets"
	}
	else
	{
		"$build/assets"
	}
	foreach ($name in $assets)
	{
		if ($kind -eq 'web' -and $name.StartsWith('base.zip'))
		{
			continue
		}
		$source = "$assetSource/$name"
		Copy-Item -LiteralPath $source -Destination "$stage/assets" -Recurse
	}
	Get-ChildItem -LiteralPath "$build/server-audio" -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination "$stage/assets/audio" -Force }
	if ($kind -eq 'native')
	{
		Copy-Item -LiteralPath "$build/Release/native.exe" -Destination "$stage/PhoenixDemo.exe"
		if (!$VCRuntimeDirectory)
		{
			$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
			$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
			$redist = Get-ChildItem "$installation/VC/Redist/MSVC" -Directory | Where-Object { $_.Name -match '^\d+\.' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
			$VCRuntimeDirectory = "$($redist.FullName)/x64/Microsoft.VC143.CRT"
		}
		foreach ($name in @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'vcruntime140_threads.dll', 'concrt140.dll'))
		{
			Copy-Item -LiteralPath "$VCRuntimeDirectory/$name" -Destination $stage
		}
	}
	else
	{
		foreach ($name in @('index.html', 'menu.css', 'launcher.mjs', 'base-verifier.mjs', 'base-verifier-worker.js',
				'session-pump.mjs', 'session-clock.js', 'session-profile.mjs', 'runtime-environment.mjs',
				'host-worker-link.mjs',
				'runtime-capabilities.json', 'isolation-worker.js', 'phoenix_base_verifier.js', 'phoenix_base_verifier.wasm',
				'server.js', 'server.wasm', 'server.data', 'client.js', 'client.wasm', 'client.data',
				'base.zip', 'base.zip.menu.json', 'base.zip.clients'))
		{
			Copy-Item -LiteralPath "$build/$name" -Destination $stage -Recurse
		}
		& node "$repo/scripts/verify-deployment.mjs" $stage
		if ($LASTEXITCODE)
		{
			throw 'Demo site verification failed'
		}
	}
	$files = Get-ChildItem -LiteralPath $stage -File -Recurse
	if ($files.Name -match 'workshop|test-missions|test-3d|^test\.zip')
	{
		throw 'Excluded content leaked into the demo package'
	}
	$files | ForEach-Object {
		"$((Get-FileHash -LiteralPath $_.FullName).Hash)  $([IO.Path]::GetRelativePath($stage, $_.FullName))"
	} | Set-Content -LiteralPath "$stage/manifest.sha256"
	Compress-Archive -LiteralPath $stage -DestinationPath "$stage.zip"
	Write-Output "Demo package: $stage.zip"
}
